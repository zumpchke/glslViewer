#pragma once

#ifdef SUPPORT_FOR_LIBAV

#include "textureStream.h"
#include <vector>

extern "C" {
#include "miniaudio/miniaudio.h"
}


class TextureAudio : public TextureStream {
public:
    TextureAudio();
    virtual ~TextureAudio();

    virtual bool    load(const std::string &_path, bool _vFlip, TextureFilter _filter = LINEAR, TextureWrap _wrap = REPEAT);
    virtual bool    update();
    virtual void    clear();
    const int	    get_buf_len() { return m_buf_len; }
    std::vector<uint8_t> *get_buffer() { return &m_buffer_wr; }
    ma_pcm_rb 	*get_ring_buffer() { return &ring_buffer; }

private:
    ma_pcm_rb ring_buffer;
    std::vector<uint8_t> m_buffer_re;
    std::vector<uint8_t> m_buffer_wr;
    std::vector<uint8_t> m_texture;
    static const int m_buf_len = 1024;
    float* m_dft_buffer = nullptr;
};

#endif
