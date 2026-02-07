#include <drivers/keyboard.h>
#include <lib/console.h>
void simpleShell(const char c, KeyboardDriver * pKeyDriver);

KeyboardEventHandler::KeyboardEventHandler()
{

}

void KeyboardEventHandler::SetDriver(KeyboardDriver * pDriver)
{
    this->pDriver = pDriver;
}

// KeyboardDriver -----------------------------------------------------------------------
KeyboardDriver::KB_BUFFER KeyboardDriver::kb_buffer = {};

KeyboardDriver::KeyboardDriver(InterruptManager * manager, KeyboardEventHandler * handler)
    : InterruptHandler(0x21, manager), dataPort(0x60), commandPort(0x64), handler(handler)
{
    this->handler->SetDriver(this);
}

KeyboardDriver::~KeyboardDriver()
{

}

void KeyboardDriver::put_buffer(const int8_t c)
{
    if (c == '\b')
    {
        if (kb_buffer.count == 0)
        {
            return;
        }
        --kb_buffer.count;
        --kb_buffer.tail;
        if (kb_buffer.tail == -1)
        {
            kb_buffer.tail = 254;
        }
        return;
    }
    if(kb_buffer.count < 255)
    {
        ++kb_buffer.count;
        kb_buffer.buf[kb_buffer.tail++] = c;
        if (kb_buffer.tail == 255)
        {
            kb_buffer.tail = 0;
        }
    }
}

int8_t * KeyboardDriver::get_buffer(int8_t * buffer)
{
    int16_t i = 0;
    int8_t c = kb_buffer.buf[kb_buffer.head];
    buffer[i] = c;
    while(kb_buffer.count > 0 && c != '\n')
    {
        ++kb_buffer.head;
        --kb_buffer.count;
        c = kb_buffer.buf[kb_buffer.head];
        buffer[++i] = c;
        if (kb_buffer.head == 255)
        {
            kb_buffer.head = 0;
        }
    }
    ++kb_buffer.head;
    --kb_buffer.count;
    if(kb_buffer.head == 255)
    {
        kb_buffer.head = 0;
    }
    buffer[i] = '\0';

    return buffer;
}


uint32_t KeyboardDriver::HandleInterrupt(uint32_t esp)
{
    uint8_t key = dataPort.Read();
    if (handler == 0)
    {
        return 0;
    }
    static bool shift = false;
    static bool caps_lock = false;
    static bool ctrl = false;
    static bool alt = false;

    char msg[2] = {'\0'};
    msg[0] = 'a';
    
    switch (key) {
    case 0x10: // Q
        if (shift || caps_lock) handler->OnKeyDown('Q'); else handler->OnKeyDown('q');
        break;
    case 0x11: // W
        if (shift || caps_lock) handler->OnKeyDown('W'); else handler->OnKeyDown('w');
        break;
    case 0x12: // E
        if (shift || caps_lock) handler->OnKeyDown('E'); else handler->OnKeyDown('e');
        break;
    case 0x13: // R
        if (shift || caps_lock) handler->OnKeyDown('R'); else handler->OnKeyDown('r');
        break;
    case 0x14: // T
        if (shift || caps_lock) handler->OnKeyDown('T'); else handler->OnKeyDown('t');
        break;
    case 0x15: // Y
        if (shift || caps_lock) handler->OnKeyDown('Y'); else handler->OnKeyDown('y');
        break;
    case 0x16: // U
        if (shift || caps_lock) handler->OnKeyDown('U'); else handler->OnKeyDown('u');
        break;
    case 0x17: // I
        if (shift || caps_lock) handler->OnKeyDown('I'); else handler->OnKeyDown('i');
        break;
    case 0x18: // O
        if (shift || caps_lock) handler->OnKeyDown('O'); else handler->OnKeyDown('o');
        break;
    case 0x19: // P
        if (shift || caps_lock) handler->OnKeyDown('P'); else handler->OnKeyDown('p');
        break;
    case 0x1E: // A
        if (shift || caps_lock) handler->OnKeyDown('A'); else handler->OnKeyDown('a');
        break; 
    case 0x1F: // S
        if (shift || caps_lock) handler->OnKeyDown('S'); else handler->OnKeyDown('s');
        break;
    case 0x20: // D
        if (shift || caps_lock) handler->OnKeyDown('D'); else handler->OnKeyDown('d');
        break;
    case 0x21: // F
        if (shift || caps_lock) handler->OnKeyDown('F'); else handler->OnKeyDown('f');
        break;
    case 0x22: // G
        if (shift || caps_lock) handler->OnKeyDown('G'); else handler->OnKeyDown('g');
        break;
    case 0x23: // H
        if (shift || caps_lock) handler->OnKeyDown('H'); else handler->OnKeyDown('h');
        break;
    case 0x24: // J
        if (shift || caps_lock) handler->OnKeyDown('J'); else handler->OnKeyDown('j');
        break;
    case 0x25: // K
        if (shift || caps_lock) handler->OnKeyDown('K'); else handler->OnKeyDown('k');
        break;
    case 0x26: // L
        if (shift || caps_lock) handler->OnKeyDown('L'); else handler->OnKeyDown('l');
        break;
    case 0x2C: // Z
        if (shift || caps_lock) handler->OnKeyDown('Z'); else handler->OnKeyDown('z');
        break;
    case 0x2D: // X
        if (shift || caps_lock) handler->OnKeyDown('X'); else handler->OnKeyDown('x');
        break;
    case 0x2E: // C
        if (shift || caps_lock) handler->OnKeyDown('C'); else handler->OnKeyDown('c');
        break;
    case 0x2F: // V
        if (shift || caps_lock) handler->OnKeyDown('V'); else handler->OnKeyDown('v');
        break;
    case 0x30: // B
        if (shift || caps_lock) handler->OnKeyDown('B'); else handler->OnKeyDown('b');
        break;
    case 0x31: // N
        if (shift || caps_lock) handler->OnKeyDown('N'); else handler->OnKeyDown('n');
        break;
    case 0x32: // M
        if (shift || caps_lock) handler->OnKeyDown('M'); else handler->OnKeyDown('m');
        break;
    case 0x02: // 1
        if (shift) handler->OnKeyDown('!'); else handler->OnKeyDown('1');
        break;
    case 0x03: // 2
        if (shift) handler->OnKeyDown('@'); else handler->OnKeyDown('2');
        break;
    case 0x04: // 3
        if (shift) handler->OnKeyDown('#'); else handler->OnKeyDown('3');
        break;
    case 0x05: // 4
        if (shift) handler->OnKeyDown('$'); else handler->OnKeyDown('4');
        break;
    case 0x06: // 5
        if (shift) handler->OnKeyDown('%'); else handler->OnKeyDown('5');
        break;
    case 0x07: // 6
        if (shift) handler->OnKeyDown('^'); else handler->OnKeyDown('6');
        break;
    case 0x08: // 7
        if (shift) handler->OnKeyDown('&'); else handler->OnKeyDown('7');
        break;
    case 0x09: // 8
        if (shift) handler->OnKeyDown('*'); else handler->OnKeyDown('8');
        break;
    case 0x0A: // 9
        if (shift) handler->OnKeyDown('('); else handler->OnKeyDown('9');
        break;
    case 0x0B: // 0
        if (shift) handler->OnKeyDown(')'); else handler->OnKeyDown('0');
        break;
    case 0x0C: // -
        if (shift) handler->OnKeyDown('_'); else handler->OnKeyDown('-');
        break;
    case 0x0D: // =
        if (shift) handler->OnKeyDown('+'); else handler->OnKeyDown('=');
        break;
    case 0x1A: // [
        if (shift) handler->OnKeyDown('{'); else handler->OnKeyDown('[');
        break;
    case 0x1B: // ]
        if (shift) handler->OnKeyDown('}'); else handler->OnKeyDown(']');
        break;
    case 0x2B: // |
        if (shift) handler->OnKeyDown('|'); else handler->OnKeyDown('\\');
        break;
    case 0x27: // ;
        if (shift) handler->OnKeyDown(':'); else handler->OnKeyDown(';');
        break;
    case 0x28: // '
        if (shift) handler->OnKeyDown('"'); else handler->OnKeyDown('\'');
        break;
    case 0x33: // ,
        if (shift) handler->OnKeyDown('<'); else handler->OnKeyDown(',');
        break;
    case 0x34: // .
        if (shift) handler->OnKeyDown('>'); else handler->OnKeyDown('.');
        break;
    case 0x35: // /
        if (shift) handler->OnKeyDown('?'); else handler->OnKeyDown('/');
        break;
    case 0x29: // `
        if (shift) handler->OnKeyDown('~'); else handler->OnKeyDown('`');
        break;
    case 0x37: // *
        handler->OnKeyDown('*');
        break;
    case 0x4E: // +
        handler->OnKeyDown('+');
        break;
    case 0x4A: // -
        handler->OnKeyDown('-');
        break;
    case 0x1C: // Enter
        handler->OnKeyDown('\n');
        simpleShell('\n', this);
        break;
    case 0x39: // Space
        handler->OnKeyDown(' ');
        break;
    case 0x2A: // Left Shift down
    case 0x36: // Right Shift down
        shift = true;
        break;
    case 0xAA: // Left Shift up
    case 0xB6: // Right Shift up
        shift = false;
        break;
    case 0x3A: // caps lock
        caps_lock = !caps_lock;
        break;
    case 0x1D: // Left Ctrl down
        ctrl = true;
        break;
    case 0x9D: // Left Ctrl up
        ctrl = false;
        break;
    case 0x38: // Left Alt down
        alt = true;
        break;
    case 0xB8: // Left Alt up
        alt = false;
        break;
    case 0x5B:
        break;
    case 0x0E:
        //printf("\b");
        handler->OnKeyDown('\b');
        break;
    default:
        if (key < 0x80) 
        {
            printf(" keyboard 0x");
            printfHex(key);
        }
        break;
    }

    return esp;
}

void KeyboardDriver::Activate()
{
    while(commandPort.Read() & 0x1)
    {
        dataPort.Read();
    }
    commandPort.Write(0xAE); //打开键盘中断
    commandPort.Write(0x20); //get current state
    uint8_t status = (dataPort.Read() | 0x01) & ~0x10; //set the fourth bit to zero, enable keyboard interrupt
    commandPort.Write(0x60); //set state
    dataPort.Write(status);

    dataPort.Write(0xF4); //清空缓存区
}