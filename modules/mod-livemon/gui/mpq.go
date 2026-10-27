package main

import (
	"bytes"
	"compress/bzip2"
	"compress/zlib"
	"encoding/binary"
	"errors"
	"io"
	"os"
	"strings"
)

const (
	flagImplode    = 0x00000100
	flagCompress   = 0x00000200
	flagEncrypted  = 0x00010000
	flagFixKey     = 0x00020000
	flagSingleUnit = 0x01000000
	flagSectorCRC  = 0x04000000
	flagExists     = 0x80000000
	hashFree       = 0xFFFFFFFF
	hashDeleted    = 0xFFFFFFFE
)

var cryptTable = func() [0x500]uint32 {
	var table [0x500]uint32
	seed := uint32(0x00100001)
	for index1 := 0; index1 < 0x100; index1++ {
		for index2, i := index1, 0; i < 5; i, index2 = i+1, index2+0x100 {
			seed = (seed*125 + 3) % 0x2AAAAB
			high := (seed & 0xFFFF) << 16
			seed = (seed*125 + 3) % 0x2AAAAB
			table[index2] = high | (seed & 0xFFFF)
		}
	}
	return table
}()

func hashString(name string, kind uint32) uint32 {
	seed1, seed2 := uint32(0x7FED7FED), uint32(0xEEEEEEEE)
	for _, c := range []byte(strings.ToUpper(strings.ReplaceAll(name, "/", "\\"))) {
		seed1 = cryptTable[kind*0x100+uint32(c)] ^ (seed1 + seed2)
		seed2 = uint32(c) + seed1 + seed2 + (seed2 << 5) + 3
	}
	return seed1
}

func decrypt(words []uint32, key uint32) {
	seed := uint32(0xEEEEEEEE)
	for i := range words {
		seed += cryptTable[0x400+(key&0xFF)]
		value := words[i] ^ (key + seed)
		key = ((^key << 0x15) + 0x11111111) | (key >> 0x0B)
		seed = value + seed + (seed << 5) + 3
		words[i] = value
	}
}

func decryptBytes(data []byte, key uint32) {
	words := make([]uint32, len(data)/4)
	for i := range words {
		words[i] = binary.LittleEndian.Uint32(data[i*4:])
	}
	decrypt(words, key)
	for i, word := range words {
		binary.LittleEndian.PutUint32(data[i*4:], word)
	}
}

type hashEntry struct {
	Name1, Name2 uint32
	Locale       uint16
	Platform     uint16
	Block        uint32
}

type blockEntry struct {
	Offset       int64
	Packed, Size uint32
	Flags        uint32
}

type archive struct {
	file       *os.File
	base       int64
	sectorSize uint32
	hashes     []hashEntry
	blocks     []blockEntry
}

func readTable(file *os.File, offset int64, count uint32, name string) ([]uint32, error) {
	raw := make([]byte, int(count)*16)
	if _, err := file.ReadAt(raw, offset); err != nil {
		return nil, err
	}
	words := make([]uint32, count*4)
	for i := range words {
		words[i] = binary.LittleEndian.Uint32(raw[i*4:])
	}
	decrypt(words, hashString(name, 3))
	return words, nil
}

func openArchive(path string) (*archive, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	header := make([]byte, 44)
	var base int64 = -1
	for offset := int64(0); offset < 1<<26; offset += 512 {
		if _, err := file.ReadAt(header, offset); err != nil {
			break
		}
		if string(header[:4]) == "MPQ\x1a" {
			base = offset
			break
		}
	}
	if base < 0 {
		file.Close()
		return nil, errors.New("not an MPQ archive")
	}
	version := binary.LittleEndian.Uint16(header[12:])
	sectorShift := binary.LittleEndian.Uint16(header[14:])
	hashPos := int64(binary.LittleEndian.Uint32(header[16:]))
	blockPos := int64(binary.LittleEndian.Uint32(header[20:]))
	hashCount := binary.LittleEndian.Uint32(header[24:])
	blockCount := binary.LittleEndian.Uint32(header[28:])
	var hiBlockPos int64
	if version >= 1 {
		hiBlockPos = int64(binary.LittleEndian.Uint64(header[32:]))
		hashPos |= int64(binary.LittleEndian.Uint16(header[40:])) << 32
		blockPos |= int64(binary.LittleEndian.Uint16(header[42:])) << 32
	}
	hashWords, err := readTable(file, base+hashPos, hashCount, "(hash table)")
	if err != nil {
		file.Close()
		return nil, err
	}
	blockWords, err := readTable(file, base+blockPos, blockCount, "(block table)")
	if err != nil {
		file.Close()
		return nil, err
	}
	a := &archive{file: file, base: base, sectorSize: 512 << sectorShift}
	for i := uint32(0); i < hashCount; i++ {
		w := hashWords[i*4:]
		a.hashes = append(a.hashes, hashEntry{w[0], w[1], uint16(w[2]), uint16(w[2] >> 16), w[3]})
	}
	var hi []byte
	if hiBlockPos != 0 {
		hi = make([]byte, int(blockCount)*2)
		file.ReadAt(hi, base+hiBlockPos)
	}
	for i := uint32(0); i < blockCount; i++ {
		w := blockWords[i*4:]
		offset := int64(w[0])
		if hi != nil {
			offset |= int64(binary.LittleEndian.Uint16(hi[i*2:])) << 32
		}
		a.blocks = append(a.blocks, blockEntry{offset, w[1], w[2], w[3]})
	}
	return a, nil
}

func (a *archive) find(name string) (blockEntry, bool) {
	if len(a.hashes) == 0 {
		return blockEntry{}, false
	}
	mask := uint32(len(a.hashes) - 1)
	start := hashString(name, 0) & mask
	name1, name2 := hashString(name, 1), hashString(name, 2)
	for i, index := uint32(0), start; i < uint32(len(a.hashes)); i, index = i+1, (index+1)&mask {
		entry := a.hashes[index]
		if entry.Block == hashFree {
			break
		}
		if entry.Block != hashDeleted && entry.Name1 == name1 && entry.Name2 == name2 &&
			entry.Block < uint32(len(a.blocks)) && a.blocks[entry.Block].Flags&flagExists != 0 {
			return a.blocks[entry.Block], true
		}
	}
	return blockEntry{}, false
}

func unpack(data []byte, size uint32) ([]byte, error) {
	if uint32(len(data)) >= size {
		return data[:size], nil
	}
	method, body := data[0], data[1:]
	var reader io.Reader
	switch {
	case method == 0x02:
		zr, err := zlib.NewReader(bytes.NewReader(body))
		if err != nil {
			return nil, err
		}
		reader = zr
	case method == 0x10:
		reader = bzip2.NewReader(bytes.NewReader(body))
	default:
		return nil, errors.New("unsupported MPQ compression")
	}
	out := make([]byte, size)
	_, err := io.ReadFull(reader, out)
	return out, err
}

func (a *archive) read(name string) ([]byte, error) {
	block, ok := a.find(name)
	if !ok {
		return nil, os.ErrNotExist
	}
	if block.Flags&flagImplode != 0 {
		return nil, errors.New("imploded MPQ files are not supported")
	}
	raw := make([]byte, block.Packed)
	if _, err := a.file.ReadAt(raw, a.base+block.Offset); err != nil {
		return nil, err
	}
	key := uint32(0)
	if block.Flags&flagEncrypted != 0 {
		short := name[strings.LastIndexAny(name, "\\/")+1:]
		key = hashString(short, 3)
		if block.Flags&flagFixKey != 0 {
			key = (key + uint32(block.Offset)) ^ block.Size
		}
	}
	if block.Flags&flagSingleUnit != 0 {
		if key != 0 {
			decryptBytes(raw, key)
		}
		if block.Flags&flagCompress != 0 {
			return unpack(raw, block.Size)
		}
		return raw[:block.Size], nil
	}
	if block.Flags&flagCompress == 0 {
		out := make([]byte, 0, block.Size)
		for i := uint32(0); uint32(len(out)) < block.Size; i++ {
			end := min(uint32(len(out))+a.sectorSize, block.Size)
			sector := raw[len(out):end]
			if key != 0 {
				decryptBytes(sector, key+i)
			}
			out = append(out, sector...)
		}
		return out, nil
	}
	sectors := (block.Size + a.sectorSize - 1) / a.sectorSize
	entries := sectors + 1
	if block.Flags&flagSectorCRC != 0 {
		entries++
	}
	table := raw[:entries*4]
	if key != 0 {
		decryptBytes(table, key-1)
	}
	out := make([]byte, 0, block.Size)
	for i := uint32(0); i < sectors; i++ {
		from := binary.LittleEndian.Uint32(table[i*4:])
		to := binary.LittleEndian.Uint32(table[i*4+4:])
		if to > uint32(len(raw)) || from > to {
			return nil, errors.New("broken MPQ sector table")
		}
		sector := append([]byte(nil), raw[from:to]...)
		if key != 0 {
			decryptBytes(sector, key+i)
		}
		want := min(a.sectorSize, block.Size-uint32(len(out)))
		plain, err := unpack(sector, want)
		if err != nil {
			return nil, err
		}
		out = append(out, plain...)
	}
	return out, nil
}
