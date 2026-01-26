#include <drivers/handlers.h>
#include <lib/console.h>

// 键盘事件处理器：将按键输出到控制台
void PrintfKeyboardEventHandler::OnKeyDown(int8_t c)
{
    pDriver->put_buffer(c);
    char msg[2] = {'\0'};
    msg[0] = c;
    printf(msg);
}

// 鼠标事件处理器：在控制台显示鼠标位置
uint16_t * MouseToConsole::VideoMemory = (uint16_t*)0xB8000;

MouseToConsole::MouseToConsole() : x(40), y(12), x_odd(40), y_odd(12)
{
    VideoMemory[80 * y + x] = ((VideoMemory[80 * y + x] & 0xF000) >> 4) |
                              ((VideoMemory[80 * y + x] & 0x0F00) << 4) |
                              ((VideoMemory[80 * y + x] & 0x00FF));
}

void MouseToConsole::OnMouseMove(int16_t xoffset, int16_t yoffset)
{
    VideoMemory[80 * y + x] = ((VideoMemory[80 * y + x] & 0xF000) >> 4) |
                              ((VideoMemory[80 * y + x] & 0x0F00) << 4) |
                              ((VideoMemory[80 * y + x] & 0x00FF));
    x += xoffset;
    if(x < 0) x = 0;
    if(x >= 80) x = 79;
    y += yoffset;
    if(y < 0) y = 0;
    if(y >= 25) y = 24;

    VideoMemory[80 * y + x] = ((VideoMemory[80 * y + x] & 0xF000) >> 4) |
                              ((VideoMemory[80 * y + x] & 0x0F00) << 4) |
                              ((VideoMemory[80 * y + x] & 0x00FF));
}

void MouseToConsole::OnMouseDown(uint8_t button)
{
    VideoMemory[80 * y + x] = ((VideoMemory[80 * y + x] & 0xF000) >> 4) |
                              ((VideoMemory[80 * y + x] & 0x0F00) << 4) |
                              ((VideoMemory[80 * y + x] & 0x00FF));
    x_odd = x;
    y_odd = y;
}

void MouseToConsole::OnMouseUp(uint8_t button)
{
    VideoMemory[80 * y_odd + x_odd] = ((VideoMemory[80 * y_odd + x_odd] & 0xF000) >> 4) |
                                      ((VideoMemory[80 * y_odd + x_odd] & 0x0F00) << 4) |
                                      ((VideoMemory[80 * y_odd + x_odd] & 0x00FF));
}

