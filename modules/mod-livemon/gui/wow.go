package main

import (
	"bytes"
	"encoding/binary"
	"image/png"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
)

type library struct {
	folder   string
	once     sync.Once
	archives []*archive
	mu       sync.Mutex
	cache    map[string][]byte
	icons    map[uint32]string
	iconOnce sync.Once
	items    map[uint32]string
	itemOnce sync.Once
}

func newLibrary(folder string) *library {
	return &library{folder: folder, cache: map[string][]byte{}}
}

func localeFolders(data string) []string {
	var found []string
	entries, _ := os.ReadDir(data)
	for _, entry := range entries {
		if entry.IsDir() && len(entry.Name()) == 4 {
			found = append(found, entry.Name())
		}
	}
	return found
}

func (l *library) open() {
	if l.folder == "" {
		return
	}
	data := filepath.Join(l.folder, "Data")
	var low []string
	for _, name := range []string{"common.MPQ", "common-2.MPQ", "expansion.MPQ", "lichking.MPQ"} {
		low = append(low, filepath.Join(data, name))
	}
	var patches []string
	for _, locale := range localeFolders(data) {
		dir := filepath.Join(data, locale)
		for _, name := range []string{"locale-", "speech-", "expansion-locale-", "lichking-locale-"} {
			low = append(low, filepath.Join(dir, name+locale+".MPQ"))
		}
		entries, _ := os.ReadDir(dir)
		for _, entry := range entries {
			lower := strings.ToLower(entry.Name())
			if strings.HasPrefix(lower, "patch-") && strings.HasSuffix(lower, ".mpq") {
				patches = append(patches, filepath.Join(dir, entry.Name()))
			}
		}
	}
	entries, _ := os.ReadDir(data)
	for _, entry := range entries {
		lower := strings.ToLower(entry.Name())
		if (lower == "patch.mpq" || strings.HasPrefix(lower, "patch-")) && strings.HasSuffix(lower, ".mpq") {
			patches = append(patches, filepath.Join(data, entry.Name()))
		}
	}
	sort.Slice(patches, func(i, j int) bool {
		return strings.ToLower(filepath.Base(patches[i])) < strings.ToLower(filepath.Base(patches[j]))
	})
	ordered := append(low, patches...)
	for i := len(ordered) - 1; i >= 0; i-- {
		if a, err := openArchive(ordered[i]); err == nil {
			l.archives = append(l.archives, a)
		}
	}
}

func (l *library) read(name string) ([]byte, error) {
	l.once.Do(l.open)
	for _, a := range l.archives {
		if data, err := a.read(name); err == nil {
			return data, nil
		}
	}
	return nil, os.ErrNotExist
}

func (l *library) texture(name string) ([]byte, error) {
	if !strings.HasSuffix(strings.ToLower(name), ".blp") {
		name += ".blp"
	}
	key := strings.ToLower(name)
	l.mu.Lock()
	cached, ok := l.cache[key]
	l.mu.Unlock()
	if ok {
		if cached == nil {
			return nil, os.ErrNotExist
		}
		return cached, nil
	}
	var out []byte
	if data, err := l.read(name); err == nil {
		if img, err := decodeBLP(data); err == nil {
			var buffer bytes.Buffer
			if png.Encode(&buffer, img) == nil {
				out = buffer.Bytes()
			}
		}
	}
	l.mu.Lock()
	l.cache[key] = out
	l.mu.Unlock()
	if out == nil {
		return nil, os.ErrNotExist
	}
	return out, nil
}

func (l *library) stringColumn(file string, offset uint32) map[uint32]string {
	found := map[uint32]string{}
	data, err := l.read("DBFilesClient\\" + file)
	if err != nil || len(data) < 20 || string(data[:4]) != "WDBC" {
		return found
	}
	count := binary.LittleEndian.Uint32(data[4:])
	size := binary.LittleEndian.Uint32(data[12:])
	texts := 20 + count*size
	for i := uint32(0); i < count; i++ {
		row := data[20+i*size:]
		at := texts + binary.LittleEndian.Uint32(row[offset:])
		if int(at) >= len(data) {
			continue
		}
		end := bytes.IndexByte(data[at:], 0)
		if end > 0 {
			found[binary.LittleEndian.Uint32(row)] = string(data[at : int(at)+end])
		}
	}
	return found
}

func (l *library) itemIcon(display uint32) ([]byte, error) {
	l.itemOnce.Do(func() { l.items = l.stringColumn("ItemDisplayInfo.dbc", 20) })
	name, ok := l.items[display]
	if !ok {
		return nil, os.ErrNotExist
	}
	return l.texture("Interface\\Icons\\" + name)
}

func (l *library) spellIcon(id uint32) ([]byte, error) {
	l.iconOnce.Do(func() { l.icons = l.stringColumn("SpellIcon.dbc", 4) })
	path, ok := l.icons[id]
	if !ok {
		return nil, os.ErrNotExist
	}
	return l.texture(path)
}
