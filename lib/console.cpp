#include <lib/console.h>
#include <lib/port.h>

extern "C" {

// 设置VGA硬件光标位置
void setCursorPosition(uint8_t x, uint8_t y)
{
    uint16_t position = y * 80 + x;
    
    // VGA光标控制寄存器
    Port8Bit indexPort(0x3D4);  // 索引寄存器
    Port8Bit dataPort(0x3D5);   // 数据寄存器
    
    // 设置光标位置低字节
    indexPort.Write(0x0F);
    dataPort.Write((uint8_t)(position & 0xFF));
    
    // 设置光标位置高字节
    indexPort.Write(0x0E);
    dataPort.Write((uint8_t)((position >> 8) & 0xFF));
}

void printf(const int8_t * str)
{
    static int16_t * VideoMemory = (int16_t*) 0xb8000;
    static int8_t x = 0, y = 14;  // 从第15行开始（索引14）
    
    if (str[0] == '\b')
    {   
        --x;
        if (x < 0)
        {
            x = 0;
        }
        VideoMemory[80 * y + x] = 0x0700;  // 清空字符，保留背景色
        setCursorPosition(x, y);
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
    
    // 更新硬件光标位置
    setCursorPosition(x, y);
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