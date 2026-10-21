package main

import (
	"bufio"
	"embed"
	"flag"
	"fmt"
	"io"
	"io/fs"
	"log"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

//go:embed web
var web embed.FS

type settings struct {
	Server    string
	WowFolder string
	Browser   string
}

func readSettings(path string) settings {
	s := settings{Server: "http://127.0.0.1:8089"}
	file, err := os.Open(path)
	if err != nil {
		return s
	}
	defer file.Close()
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		key, value, ok := strings.Cut(scanner.Text(), "=")
		if !ok || strings.HasPrefix(strings.TrimSpace(key), "#") {
			continue
		}
		value = strings.Trim(strings.TrimSpace(value), "\"")
		switch strings.ToLower(strings.TrimSpace(key)) {
		case "server":
			s.Server = strings.TrimRight(value, "/")
		case "wowfolder":
			s.WowFolder = value
		case "browser":
			s.Browser = value
		}
	}
	return s
}

func findWowFolder(start string) string {
	for dir := start; ; dir = filepath.Dir(dir) {
		if _, err := os.Stat(filepath.Join(dir, "Data", "common.MPQ")); err == nil {
			return dir
		}
		if filepath.Dir(dir) == dir {
			return ""
		}
	}
}

func browserCandidates(configured string) []string {
	if configured != "" {
		return []string{configured}
	}
	var found []string
	for _, root := range []string{os.Getenv("ProgramFiles(x86)"), os.Getenv("ProgramFiles"), os.Getenv("LocalAppData")} {
		if root == "" {
			continue
		}
		found = append(found, filepath.Join(root, "Microsoft", "Edge", "Application", "msedge.exe"),
			filepath.Join(root, "Google", "Chrome", "Application", "chrome.exe"))
	}
	return found
}

func openWindow(url, configured string) bool {
	profile := filepath.Join(os.TempDir(), "LiveMon-window")
	for _, browser := range browserCandidates(configured) {
		if _, err := os.Stat(browser); err != nil {
			continue
		}
		cmd := exec.Command(browser, "--app="+url, "--user-data-dir="+profile, "--window-size=1400,900",
			"--no-first-run", "--no-default-browser-check")
		if err := cmd.Start(); err != nil {
			continue
		}
		opened := time.Now()
		cmd.Wait()
		return time.Since(opened) > 5*time.Second
	}
	exec.Command("rundll32", "url.dll,FileProtocolHandler", url).Start()
	return false
}

func main() {
	exe, _ := os.Executable()
	home := filepath.Dir(exe)
	s := readSettings(filepath.Join(home, "livemon.ini"))
	server := flag.String("server", s.Server, "LiveMon feed of the worldserver")
	wow := flag.String("wow", s.WowFolder, "WoW client folder (for the troll face)")
	stay := flag.Bool("stay", false, "keep serving after the window closes")
	extract := flag.String("extract", "", "model path without extension to copy out of the client, with its skins and animations")
	out := flag.String("out", filepath.Join(home, "extracted"), "folder for -extract")
	flag.Parse()
	if *wow == "" {
		*wow = findWowFolder(home)
	}

	game := newLibrary(*wow)
	if *extract != "" {
		found := game.extractModel(*extract, *out)
		report := fmt.Sprintf("%d file(s) of %s from %s written to %s\n", found, *extract, *wow, *out)
		os.MkdirAll(*out, 0o755)
		os.WriteFile(filepath.Join(*out, "extract.txt"), []byte(report), 0o644)
		return
	}
	pages, _ := fs.Sub(web, "web")
	client := &http.Client{Timeout: 5 * time.Second}
	mux := http.NewServeMux()
	mux.Handle("/", http.FileServer(http.FS(pages)))
	sendPNG := func(w http.ResponseWriter, r *http.Request, data []byte, err error) {
		if err != nil {
			http.NotFound(w, r)
			return
		}
		w.Header().Set("Content-Type", "image/png")
		w.Header().Set("Cache-Control", "max-age=86400")
		w.Write(data)
	}
	mux.HandleFunc("/wow/texture", func(w http.ResponseWriter, r *http.Request) {
		data, err := game.texture(r.URL.Query().Get("path"))
		sendPNG(w, r, data, err)
	})
	mux.HandleFunc("/wow/spellicon", func(w http.ResponseWriter, r *http.Request) {
		id, _ := strconv.ParseUint(r.URL.Query().Get("id"), 10, 32)
		data, err := game.spellIcon(uint32(id))
		sendPNG(w, r, data, err)
	})
	mux.HandleFunc("/wow/itemicon", func(w http.ResponseWriter, r *http.Request) {
		id, _ := strconv.ParseUint(r.URL.Query().Get("display"), 10, 32)
		data, err := game.itemIcon(uint32(id))
		sendPNG(w, r, data, err)
	})
	mux.HandleFunc("/wow/font", func(w http.ResponseWriter, r *http.Request) {
		data, err := game.read(r.URL.Query().Get("path"))
		if err != nil {
			http.NotFound(w, r)
			return
		}
		w.Header().Set("Content-Type", "font/ttf")
		w.Header().Set("Cache-Control", "max-age=86400")
		w.Write(data)
	})
	mux.HandleFunc("/feed/", func(w http.ResponseWriter, r *http.Request) {
		target := *server + strings.TrimPrefix(r.URL.Path, "/feed") + "?" + r.URL.RawQuery
		response, err := client.Get(target)
		if err != nil {
			w.Header().Set("Content-Type", "application/json")
			w.WriteHeader(http.StatusBadGateway)
			fmt.Fprintf(w, `{"offline":true,"server":%q}`, *server)
			return
		}
		defer response.Body.Close()
		w.Header().Set("Content-Type", "application/json")
		w.Header().Set("Cache-Control", "no-store")
		io.Copy(w, response.Body)
	})

	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		log.Fatal(err)
	}
	url := "http://" + listener.Addr().String() + "/"
	go http.Serve(listener, mux)
	log.Printf("LiveMon window on %s, feed %s", url, *server)
	if openWindow(url, s.Browser) && !*stay {
		return
	}
	select {}
}
