#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <jpeglib.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace jpegres {

inline void throw_error(j_common_ptr cinfo) {
    char msg[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, msg);
    throw std::runtime_error(msg);
}
inline void silent(j_common_ptr, int) {} // ignore "corrupt data" warnings

// TX side: lossless transcode, one restart marker per MCU row.
inline std::vector<uint8_t> add_restart_markers(const std::vector<uint8_t>& in) {
    jpeg_error_mgr jerr;
    jpeg_decompress_struct src{};
    jpeg_compress_struct dst{};
    src.err = dst.err = jpeg_std_error(&jerr);
    jerr.error_exit = throw_error;

    jpeg_create_decompress(&src);
    jpeg_create_compress(&dst);
    jpeg_mem_src(&src, in.data(), in.size());
    jpeg_read_header(&src, TRUE);
    jvirt_barray_ptr* coefs = jpeg_read_coefficients(&src);

    const unsigned mcu_w = 8 * src.max_h_samp_factor;
    const unsigned mcus_per_row = (src.image_width + mcu_w - 1) / mcu_w;

    jpeg_copy_critical_parameters(&src, &dst);
    dst.restart_interval = mcus_per_row; // set AFTER copy_critical_parameters

    unsigned char* buf = nullptr;
    unsigned long size = 0;
    jpeg_mem_dest(&dst, &buf, &size);
    jpeg_write_coefficients(&dst, coefs);
    jpeg_finish_compress(&dst);

    std::vector<uint8_t> out(buf, buf + size);
    free(buf);
    jpeg_destroy_compress(&dst);
    jpeg_finish_decompress(&src);
    jpeg_destroy_decompress(&src);
    return out;
}

// Intervals (between RST markers) of the *transmitted* file that contain a lost byte.
// Interval k = restart interval k = image row k.
inline std::vector<uint8_t> find_damaged_intervals(const std::vector<uint8_t>& tx,
                                                   const std::vector<uint8_t>& lost) {
    // Walk header segments to find the start of the entropy-coded data (after SOS)
    std::size_t pos = 2; // skip SOI
    std::size_t scan_start = 0;
    while (pos + 3 < tx.size() && tx[pos] == 0xFF) {
        const std::size_t len = (tx[pos + 2] << 8) | tx[pos + 3];
        if (tx[pos + 1] == 0xDA) {
            scan_start = pos + 2 + len;
            break;
        }
        pos += 2 + len;
    }
    if (scan_start == 0)
        throw std::runtime_error("SOS marker not found");

    auto any_lost = [&](std::size_t lo, std::size_t hi) {
        hi = std::min(hi, lost.size());
        return std::find(lost.begin() + lo, lost.begin() + hi, uint8_t{1}) != lost.begin() + hi;
    };

    std::vector<uint8_t> damaged;
    std::size_t begin = scan_start;
    for (std::size_t i = scan_start; i + 1 < tx.size(); ++i) {
        if (tx[i] != 0xFF)
            continue;
        const uint8_t m = tx[i + 1];
        if (m >= 0xD0 && m <= 0xD7) { // RSTn: end of an interval (marker included)
            damaged.push_back(any_lost(begin, i + 2));
            begin = i + 2;
            ++i;
        } else if (m == 0xD9) { // EOI: last interval
            damaged.push_back(any_lost(begin, i + 2));
            break;
        } else if (m == 0x00) { // byte stuffing
            ++i;
        }
    }
    return damaged;
}

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgb;
};

// RX side: decode the (zero-filled) file and paint damaged rows magenta.
inline Image decode_and_mark(const std::vector<uint8_t>& rx, const std::vector<uint8_t>& damaged) {
    jpeg_error_mgr jerr;
    jpeg_decompress_struct c{};
    c.err = jpeg_std_error(&jerr);
    jerr.error_exit = throw_error;
    jerr.emit_message = silent;

    jpeg_create_decompress(&c);
    jpeg_mem_src(&c, rx.data(), rx.size());
    jpeg_read_header(&c, TRUE); // throws if the header packets were lost
    c.out_color_space = JCS_RGB;
    jpeg_start_decompress(&c);

    Image img;
    img.width = c.output_width;
    img.height = c.output_height;
    img.rgb.assign(std::size_t(img.width) * img.height * 3, 0);

    const std::size_t stride = std::size_t(img.width) * 3;
    while (c.output_scanline < c.output_height) {
        JSAMPROW row = &img.rgb[c.output_scanline * stride];
        jpeg_read_scanlines(&c, &row, 1);
    }

    const unsigned mcu_w = 8 * c.max_h_samp_factor;
    const unsigned mcu_h = 8 * c.max_v_samp_factor;
    const unsigned mcus_per_row = (c.image_width + mcu_w - 1) / mcu_w;
    const unsigned R = c.restart_interval;

    if (R != 0) {
        for (std::size_t k = 0; k < damaged.size(); ++k) {
            if (!damaged[k])
                continue;
            const std::size_t row_a = (k * R) / mcus_per_row;
            const std::size_t row_b = ((k + 1) * R - 1) / mcus_per_row;
            const std::size_t y0 = row_a * mcu_h;
            const std::size_t y1 = std::min<std::size_t>((row_b + 1) * mcu_h, img.height);
            for (std::size_t y = y0; y < y1; ++y)
                for (int x = 0; x < img.width; ++x) {
                    uint8_t* p = &img.rgb[y * stride + x * 3];
                    p[0] = 255;
                    p[1] = 0;
                    p[2] = 255; // magenta; use 0,0,0 for black
                }
        }
    }

    jpeg_finish_decompress(&c);
    jpeg_destroy_decompress(&c);
    return img;
}

inline void save_ppm(const jpegres::Image& img, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("Could not create output file");
    f << "P6\n" << img.width << " " << img.height << "\n255\n";
    f.write(reinterpret_cast<const char*>(img.rgb.data()), img.rgb.size());
}

} // namespace jpegres