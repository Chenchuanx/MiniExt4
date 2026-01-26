#ifndef __HANDLERS_H_
#define __HANDLERS_H_

#include <drivers/keyboard.h>
#include <drivers/mouse.h>

// 键盘事件处理器：将按键输出到控制台
class PrintfKeyboardEventHandler : public KeyboardEventHandler
{
public:
    void OnKeyDown(int8_t c);
};

// 鼠标事件处理器：在控制台显示鼠标位置
class MouseToConsole: public MouseEventHandler
{
    int8_t x, y;
    int8_t x_odd, y_odd;
    static uint16_t * VideoMemory;
public:
    MouseToConsole();
    void OnMouseMove(int16_t xoffset, int16_t yoffset);
    void OnMouseDown(uint8_t button);
    void OnMouseUp(uint8_t button);
};

#endif

