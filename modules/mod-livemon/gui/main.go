package main

import (
	"bufio"
	"bytes"
	"embed"
	"flag"
	"fmt"
	"image/png"
	"io"
	"io/fs"
	"log"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
)

//go:embed web
var web embed.FS

var trollFaces = []string{
	"Interface\\Icons\\Achievement_Character_Troll_Male.blp",
	"Interface\\Icons\\Achievement_Character_Troll_Female.blp",
	"Interface\\CharacterFrame\\TemporaryPortrait-Male-Troll.blp",
}

var baseArchives = []string{"common.MPQ", "common-2.MPQ", "expansion.MPQ", "lichking.MPQ", "patch.MPQ",
	"patch-2.MPQ", "patch-3.MPQ"}

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

func archivesOf(wow string) []string {
	data := filepath.Join(wow, "Data")
	var paths []string
	for _, name := range baseArchives {
		paths = append(paths, filepath.Join(data, name))
	}
	if entries, err := os.ReadDir(data); err == nil {
		var patches []string
		for _, entry := range entries {
			name := entry.Name()
			lower := strings.ToLower(name)
			if strings.HasPrefix(lower, "patch-") && strings.HasSuffix(lower, ".mpq") &&
				lower != "patch-2.mpq" && lower != "patch-3.mpq" {
				patches = append(patches, name)
			}
		}
		sort.Slice(patches, func(i, j int) bool { return strings.ToLower(patches[i]) < strings.ToLower(patches[j]) })
		for _, name := range patches {
			paths = append(paths, filepath.Join(data, name))
		}
	}
	for i, j := 0, len(paths)-1; i < j; i, j = i+1, j-1 {
		paths[i], paths[j] = paths[j], paths[i]
	}
	return paths
}

func trollFace(wow string) ([]byte, error) {
	if wow == "" {
		return nil, fmt.Errorf("no WoW folder found")
	}
	for _, path := range archivesOf(wow) {
		a, err := openArchive(path)
		if err != nil {
			continue
		}
		for _, wanted := range trollFaces {
			data, err := a.read(wanted)
			if err != nil {
				continue
			}
			img, err := decodeBLP(data)
			if err != nil {
				continue
			}
			a.file.Close()
			var out bytes.Buffer
			if err := png.Encode(&out, img); err != nil {
				return nil, err
			}
			log.Printf("troll face: %s from %s", wanted, filepath.Base(path))
			return out.Bytes(), nil
		}
		a.file.Close()
	}
	return nil, fmt.Errorf("no troll face in %s", wow)
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
	flag.Parse()
	if *wow == "" {
		*wow = findWowFolder(home)
	}

	var faceOnce sync.Once
	var face []byte
	pages, _ := fs.Sub(web, "web")
	client := &http.Client{Timeout: 5 * time.Second}
	mux := http.NewServeMux()
	mux.Handle("/", http.FileServer(http.FS(pages)))
	mux.HandleFunc("/troll.png", func(w http.ResponseWriter, r *http.Request) {
		faceOnce.Do(func() {
			var err error
			if face, err = trollFace(*wow); err != nil {
				log.Print(err)
			}
		})
		if face == nil {
			http.NotFound(w, r)
			return
		}
		w.Header().Set("Content-Type", "image/png")
		w.Write(face)
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
