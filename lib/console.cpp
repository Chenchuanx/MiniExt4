#include <lib/console.h>
#include <lib/port.h>

extern "C" {

void printf(const int8_t * str)
{
    static int16_t * VideoMemory = (int16_t*) 0xb8000;
    static int8_t x = 0, y = 14;  // 从第15行开始（索引14）

    // 先恢复上一个光标位置的正常颜色（灰字黑底：0x07）
    VideoMemory[80 * y + x] = (VideoMemory[80 * y + x] & 0x00FF) | 0x0700;

    // 处理退格：更新坐标 + 擦除字符，然后重新绘制软光标
    if (str[0] == '\b')
    {
        --x;
        if (x < 0)
        {
            x = 0;
        }
        VideoMemory[80 * y + x] = 0x0700;  // 清空字符，灰字黑底

        // 在新位置绘制软光标（反色：白底黑字 0x70）
        VideoMemory[80 * y + x] = (VideoMemory[80 * y + x] & 0x00FF) | 0x7000;
        return;
    }

    for(int32_t i = 0; str[i] != '\0'; ++i)
    {
        switch(str[i])
        {
            case '\n':
                ++y;
                x = 0;
                break;
            default:
                VideoMemory[80 * y + x] = (VideoMemory[80 * y + x] & 0xFF00) | str[i];
                ++x;
        }

        if(x >= 80)
        {
            ++y;
            x = 0;
        }

        // 屏幕滚动：当到达最后一行时，向上滚动
        if(y >= 25)
        {
            // 向上滚动：将第 1-24 行复制到第 0-23 行
            for(int8_t row = 0; row < 24; ++row)
            {
                for(int8_t col = 0; col < 80; ++col)
                {
                    VideoMemory[80 * row + col] = VideoMemory[80 * (row + 1) + col];
                }
            }
            // 清空最后一行（第 24 行）
            for(int8_t col = 0; col < 80; ++col)
            {
                VideoMemory[80 * 24 + col] = 0x0700;  // 空格字符，灰色背景
            }
            // 光标移到最后一行
            y = 24;
        }
    }

    // 在当前 (x, y) 位置绘制软光标（白色块：白底黑字）
    VideoMemory[80 * y + x] = (VideoMemory[80 * y + x] & 0x00FF) | 0x7000;
}

void printfHex(const uint8_t num)
{
    uint8_t c = num;
    static char msg[3] = {'0'};
    const char * hex = "0123456789ABCDEF";
    msg[0] = hex[(c >> 4) & 0xF];
    msg[1] = hex[c & 0xF];
    msg[2] = '\0';
    printf(msg);
}

} /* extern "C" */