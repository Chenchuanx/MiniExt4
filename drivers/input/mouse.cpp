#include <drivers/mouse.h>
#include <lib/printf.h>

MouseEventHandler::MouseEventHandler() { }

void MouseEventHandler::OnMouseDown(uint8_t button)
{

}

void MouseEventHandler::OnMouseUp(uint8_t button)
{


}

void MouseEventHandler::OnMouseMove(int16_t xoffset, int16_t yoffset)
{

}

MouseDriver::MouseDriver(InterruptManager * manager, MouseEventHandler * handler)
    : InterruptHandler(0x2C, manager), dataPort(0x60), commandPort(0x64), handler(handler), offset(0), buttons(0)
{

}

MouseDriver::~MouseDriver()
{

}

uint32_t MouseDriver::HandleInterrupt(uint32_t esp)
{
    uint8_t status = commandPort.Read();
    if(!(status & 0x20) || handler == nullptr)
    {
        return esp;
    }
    buffer[offset] = dataPort.Read();
    offset = (offset + 1) % 3;

    if(offset == 0)
    {
        if(buffer[1] != 0 || buffer[2] != 0)
        {
            handler->OnMouseMove(buffer[1], -buffer[2]);
        }
        for(uint8_t i = 0; i < 3; ++i)
        {
            if((buffer[0] & (1 << i)) != (buttons & (1 << i)))
            {
                if(buttons & (1<<i))
                {
                    handler->OnMouseUp(i + 1);
                }
                else{
                    handler->OnMouseDown(i + 1);
                }

            }
        }
        buttons = buffer[0];
    }

    return esp;
}

void MouseDriver::Activate()
{
    commandPort.Write(0xAE); 
    commandPort.Write(0x20); 
    uint8_t status = (dataPort.Read() | 0x02) & ~0x20;
    commandPort.Write(0x60);
    dataPort.Write(status);

    commandPort.Write(0xD4);
    dataPort.Write(0xF4);
    dataPort.Read();
}


