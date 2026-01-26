#ifndef __KEYBOARD_H_
#define __KEYBOARD_H_

#include <linux/types.h>
#include <lib/port.h>
#include <kernel/interrupts.h>
#include <drivers/driver.h>

class KeyboardDriver;

class KeyboardEventHandler
{
public:
    KeyboardEventHandler();
    
    void SetDriver(KeyboardDriver * pDriver);
    virtual void OnKeyDown(char) { }
    virtual void OnKeyUp(char) { }  
protected:
    KeyboardDriver * pDriver;
};

class KeyboardDriver : public InterruptHandler, public Driver
{
public:
    KeyboardDriver(InterruptManager * manager, KeyboardEventHandler * handler);
    ~KeyboardDriver();

    virtual void put_buffer(const int8_t c);
    virtual int8_t * get_buffer(int8_t * buffer);
    uint32_t HandleInterrupt(uint32_t esp);
    void Activate();

private:
    Port8Bit dataPort;
    Port8Bit commandPort;

    struct KB_BUFFER
    {
        uint8_t head = 0;
        uint8_t tail = 0;
        uint8_t count = 0;
        uint8_t buf[256];
    };
    
    static KB_BUFFER kb_buffer;
    KeyboardEventHandler * handler;
};


#endif