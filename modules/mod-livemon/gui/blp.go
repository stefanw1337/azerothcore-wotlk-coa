package main

import (
	"encoding/binary"
	"errors"
	"image"
	"image/color"
)

func rgb565(value uint16) color.NRGBA {
	r, g, b := uint8(value>>11&0x1F), uint8(value>>5&0x3F), uint8(value&0x1F)
	return color.NRGBA{r<<3 | r>>2, g<<2 | g>>4, b<<3 | b>>2, 255}
}

func mix(a, b color.NRGBA, wa, wb, total int) color.NRGBA {
	return color.NRGBA{uint8((int(a.R)*wa + int(b.R)*wb) / total), uint8((int(a.G)*wa + int(b.G)*wb) / total),
		uint8((int(a.B)*wa + int(b.B)*wb) / total), 255}
}

func colorBlock(data []byte, opaque bool) [16]color.NRGBA {
	c0, c1 := binary.LittleEndian.Uint16(data), binary.LittleEndian.Uint16(data[2:])
	a, b := rgb565(c0), rgb565(c1)
	palette := [4]color.NRGBA{a, b}
	if c0 > c1 || opaque {
		palette[2], palette[3] = mix(a, b, 2, 1, 3), mix(a, b, 1, 2, 3)
	} else {
		palette[2], palette[3] = mix(a, b, 1, 1, 2), color.NRGBA{}
	}
	bits := binary.LittleEndian.Uint32(data[4:])
	var out [16]color.NRGBA
	for i := range out {
		out[i] = palette[bits>>(2*i)&3]
	}
	return out
}

func alphaDXT5(data []byte) [16]uint8 {
	a0, a1 := int(data[0]), int(data[1])
	var levels [8]int
	levels[0], levels[1] = a0, a1
	if a0 > a1 {
		for i := 1; i < 7; i++ {
			levels[i+1] = ((7-i)*a0 + i*a1) / 7
		}
	} else {
		for i := 1; i < 5; i++ {
			levels[i+1] = ((5-i)*a0 + i*a1) / 5
		}
		levels[6], levels[7] = 0, 255
	}
	var bits uint64
	for i := 0; i < 6; i++ {
		bits |= uint64(data[2+i]) << (8 * i)
	}
	var out [16]uint8
	for i := range out {
		out[i] = uint8(levels[bits>>(3*i)&7])
	}
	return out
}

func decodeBLP(data []byte) (image.Image, error) {
	if len(data) < 1172 || string(data[:4]) != "BLP2" {
		return nil, errors.New("not a BLP2 texture")
	}
	compression, alphaDepth, alphaType := data[8], data[9], data[10]
	width, height := int(binary.LittleEndian.Uint32(data[12:])), int(binary.LittleEndian.Uint32(data[16:]))
	offset, size := binary.LittleEndian.Uint32(data[20:]), binary.LittleEndian.Uint32(data[84:])
	if uint64(offset)+uint64(size) > uint64(len(data)) {
		return nil, errors.New("broken BLP texture")
	}
	pixels := data[offset : offset+size]
	img := image.NewNRGBA(image.Rect(0, 0, width, height))
	switch compression {
	case 1:
		count := width * height
		for i := 0; i < count && i < len(pixels); i++ {
			entry := data[148+int(pixels[i])*4:]
			alpha := uint8(255)
			switch alphaDepth {
			case 1:
				if count+i/8 < len(pixels) {
					alpha = (pixels[count+i/8] >> (i % 8) & 1) * 255
				}
			case 4:
				if count+i/2 < len(pixels) {
					alpha = (pixels[count+i/2] >> (4 * (i % 2)) & 0xF) * 17
				}
			case 8:
				if count+i < len(pixels) {
					alpha = pixels[count+i]
				}
			}
			img.SetNRGBA(i%width, i/width, color.NRGBA{entry[2], entry[1], entry[0], alpha})
		}
	case 2:
		blockSize := 16
		if alphaType == 0 {
			blockSize = 8
		}
		at := 0
		for by := 0; by < (height+3)/4; by++ {
			for bx := 0; bx < (width+3)/4; bx++ {
				if at+blockSize > len(pixels) {
					return img, nil
				}
				block := pixels[at : at+blockSize]
				at += blockSize
				var colors [16]color.NRGBA
				switch {
				case alphaType == 0:
					colors = colorBlock(block, alphaDepth == 0)
				case alphaType == 1:
					colors = colorBlock(block[8:], true)
					for i := range colors {
						colors[i].A = (block[i/2] >> (4 * (i % 2)) & 0xF) * 17
					}
				default:
					colors = colorBlock(block[8:], true)
					alphas := alphaDXT5(block)
					for i := range colors {
						colors[i].A = alphas[i]
					}
				}
				for i, c := range colors {
					x, y := bx*4+i%4, by*4+i/4
					if x < width && y < height {
						img.SetNRGBA(x, y, c)
					}
				}
			}
		}
	case 3:
		for i := 0; i < width*height && i*4+3 < len(pixels); i++ {
			p := pixels[i*4:]
			img.SetNRGBA(i%width, i/width, color.NRGBA{p[2], p[1], p[0], p[3]})
		}
	default:
		return nil, errors.New("unsupported BLP compression")
	}
	return img, nil
}
