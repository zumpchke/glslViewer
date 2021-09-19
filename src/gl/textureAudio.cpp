#include "textureAudio.h"
#include "tools/text.h"

#include <iostream>
#include <mutex>

#ifdef SUPPORT_FOR_LIBAV

#define MA_NO_DECODING 
#define MA_NO_ENCODING 
#define MA_NO_WAV
#define MA_NO_FLAC
#define MA_NO_MP3
//#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION

extern "C" {
#include <libavcodec/avfft.h>
#include <libavutil/mem.h>
#include "miniaudio/miniaudio.h"
}

ma_device_config a_deviceConfig;
ma_device a_device;
static RDFTContext *ctx;
ma_context context;
ma_device_info* pPlaybackDeviceInfos;
ma_device_info* pCaptureDeviceInfos;
ma_waveform sineWave;
ma_waveform_config sineWaveConfig;

std::mutex mtx;

void data_collect_callback_fixed(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    std::vector<uint8_t> * m_buffer = (std::vector<uint8_t>*)((TextureAudio *)pDevice->pUserData)->get_buffer();
    uint8_t * m_buffer_wr = m_buffer->data();

    (void)pDevice;
    (void)pOutput;
    assert(frameCount == 1024);
    std::lock_guard<std::mutex> lock(mtx);
    memcpy(m_buffer_wr, pInput, frameCount);
}

void data_collect_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {

    uint32_t frames_processed = 0;
    uint32_t frames_available = 0;
    const int m_buf_len = (int)((TextureAudio *)pDevice->pUserData)->get_buf_len();
    uint8_t *running_input = (uint8_t *) pInput;
    ma_pcm_rb *ring_buffer = (ma_pcm_rb *)((TextureAudio *)pDevice->pUserData)->get_ring_buffer();

    while (frames_processed < frameCount) {
        uint32_t frames_remaining = frameCount - frames_processed;

        // try to write into ring buffer
        frames_available = ma_pcm_rb_available_write(ring_buffer);

        if (frames_available > 0) {
            uint32_t to_write = std::min(frames_remaining, frames_available);

            void *pWriteBuffer;
            ma_pcm_rb_acquire_write(ring_buffer, &to_write, &pWriteBuffer);
            {
                //ma_waveform_read_pcm_frames(&sineWave, pWriteBuffer, to_write);
                memcpy(pWriteBuffer, running_input, to_write);
            }
            ma_pcm_rb_commit_write(ring_buffer, to_write, pWriteBuffer);

            frames_processed += to_write;
            running_input += to_write;

        } else {
            // ring buffer is full now. fire fixed callback
            uint32_t to_read = ma_pcm_rb_available_read(ring_buffer);
            assert((int)to_read == m_buf_len);
            void *pReadBuffer;

            //ma_pcm_rb_reset(ring_buffer);
            ma_pcm_rb_acquire_read(ring_buffer, &to_read, &pReadBuffer);
            {
                data_collect_callback_fixed(pDevice, NULL, pReadBuffer, to_read);
            }
            ma_pcm_rb_commit_read(ring_buffer, to_read, pReadBuffer);

            assert(ma_pcm_rb_available_write(ring_buffer) == m_buf_len);
            ma_pcm_rb_reset(ring_buffer);
        }
    }
}

TextureAudio::TextureAudio(): TextureStream() {
    // texture size
    m_width = 256;
    m_height = 1;
    m_texture.resize(m_width * m_height * 4, 0);
    m_dft_buffer = (float*)av_malloc_array(sizeof(float), m_buf_len);
    m_buffer_wr.resize(m_buf_len, 0);
    m_buffer_re.resize(m_buf_len, 0);
}

TextureAudio::~TextureAudio() {
    ma_pcm_rb_uninit(&ring_buffer);
    this->clear();
}

bool TextureAudio::load(const std::string &_device_id_str, bool _vFlip, TextureFilter _filter, TextureWrap _wrap) {

printf("in load()!\n");

    ma_uint32 playbackDeviceCount = 0;
    ma_uint32 captureDeviceCount = 0;
    int default_device_id = -1;
    int device_id = 0;
    int _device_id_int = -1;


    if (isInt(_device_id_str)) {
        _device_id_int = toInt(_device_id_str);
    }
    else {
        std::cout << "Device id " << _device_id_str << " is incorrect, default device will be used.\n";
    }

    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
        std::cout <<  "Failed to initialize context.\n";
        return false;
    }

    if (ma_context_get_devices(&context, &pPlaybackDeviceInfos, &playbackDeviceCount, &pCaptureDeviceInfos, &captureDeviceCount) != MA_SUCCESS) {
        std::cout << "Failed to retrieve device information.\n";
        ma_context_uninit(&context);
        return false;
    }

    std::cout << "Capture devices available\n";
    for (ma_uint32 iDevice = 0; iDevice < captureDeviceCount; ++iDevice) {
        if (pCaptureDeviceInfos[iDevice].isDefault == true) {
            default_device_id = iDevice;
        }
        std::cout << "    " << iDevice << ": " << pCaptureDeviceInfos[iDevice].name << "\n";
    }

    // there is no default (miniaudio bug) or no capture devices
    if (default_device_id == -1 || captureDeviceCount == 0) {
        std::cout << "No capture devices available.\n";
        ma_context_uninit(&context);
        return false;
    }

    if (_device_id_int == -1) {
        device_id = default_device_id;
    }
    else if (_device_id_int < -1 || _device_id_int > int(captureDeviceCount - 1)) {
        // warning if non-default device id is incorrect
        std::cout << "Device id " << _device_id_int << " is incorrect, default device will be used.\n";
        device_id = default_device_id;
    }
    else {
        device_id = _device_id_int;
    }

    std::cout << "Loading capture device\n";
    std::cout << "    " << device_id << ": " << pCaptureDeviceInfos[device_id].name << "\n";

    // set up audio format
    a_deviceConfig = ma_device_config_init(ma_device_type_capture);
    a_deviceConfig.capture.pDeviceID = &pCaptureDeviceInfos[device_id].id;
    a_deviceConfig.capture.format   = ma_format_u8;
    a_deviceConfig.capture.channels = 1;
    a_deviceConfig.sampleRate       = 44100;
    a_deviceConfig.dataCallback     = data_collect_callback;
    a_deviceConfig.pUserData        = this;

    // set up ring buffer
    ma_pcm_rb_init(ma_format_u8, 1, 1024, NULL, NULL, &ring_buffer);

    sineWaveConfig = ma_waveform_config_init(ma_format_u8, 1, 44100, ma_waveform_type_sine, 0.2, 100);
    ma_waveform_init(&sineWaveConfig, &sineWave);

    // init default capture device
    if (ma_device_init(&context, &a_deviceConfig, &a_device) != MA_SUCCESS) {
        ma_context_uninit(&context);
        std::cout << "Failed to initialize capture device." << std::endl;
        return false;
    }

    // start capture device
    if (ma_device_start(&a_device) != MA_SUCCESS) {
        ma_device_uninit(&a_device);
        ma_context_uninit(&context);
        std::cout << "Failed to start device."  << std::endl;
        return false;
    }

    // init dft calculator
    ctx = av_rdft_init((int) log2(m_buf_len), DFT_R2C);
    if (!ctx) {
        ma_device_uninit(&a_device);
        ma_context_uninit(&context);
        std::cout << "Failed to init dft calculator."  << std::endl;
        return false;
    }

    m_filter = _filter;
    m_wrap = _wrap;

    return true;
}

bool TextureAudio::update() {

    {
        std::lock_guard<std::mutex> lock(mtx);
        m_buffer_re = m_buffer_wr;
    }

    // copy amplitude values to GREEN pixels
    for (int i = 0; i < m_width; i++) {
        m_texture[1+i*4] = m_buffer_re.at(m_buf_len - m_width + i);
    }

    // prerare samples for dtf
    for (int i = 0; i < m_buf_len; i++) {
        // hann window
        double window_modifier = (0.5 * (1 - cos(2 * M_PI * i / (m_buf_len - 1))));
        // scale value from 0-255 to -1.0-+1.0
        float value = (float)(window_modifier * ((m_buffer_re.at(i)) / 127.0f - 1.0f));
        // limit values
        if (value > 1.0) value = 1.0;
        if (value < -1.0) value = -1.0;
        m_dft_buffer[i] = value;
    }

    av_rdft_calc(ctx, m_dft_buffer);

    // copy frequency values to RED pixels
    for (int i = 0; i < m_width; i++) {
        
        // magnitude
        float im = m_dft_buffer[i*2];
        float re = m_dft_buffer[i*2 + 1];
        float mag = sqrt(im * im + re * re);

        // experimental scale factor
        float mag_scaled = sqrt(mag) * 50;

        // limit values
        // if (mag_scaled > 255.0) mag_scaled = 255.0;
        // if (mag_scaled < 0.0) mag_scaled = 0.0;

        uint8_t mag_uint8 = mag_scaled;

        auto prev_value = m_texture[4*(i)];

        // failing effect for decreasing value
        double decr_factor = 0.97;
        if (prev_value > mag_uint8) {
            m_texture[i*4] = prev_value * decr_factor;
        }
        else {
            m_texture[i*4] = mag_uint8;
        }
    }
    Texture::load(m_width, m_height, 4, 8, &m_texture[0], m_filter, m_wrap);
    return true;
}

void TextureAudio::clear() {
    ma_device_uninit(&a_device);
    ma_context_uninit(&context);

   if (ctx) {
        av_rdft_end(ctx); 
    }
    av_free(m_dft_buffer);
}

#endif
