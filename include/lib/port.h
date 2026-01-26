# ifndef __PORT_H_
# define __PORT_H_

# include <linux/types.h>

class Port
{
    protected:
    uint16_t portnumber; //端口号

    public:
    Port(uint16_t portnumber);
    ~Port();
};

class Port8Bit : public Port
{
    public:
    Port8Bit(uint16_t portnumber);
    ~Port8Bit();

    virtual void Write(uint8_t data);
    virtual uint8_t Read();
};

class Port8BitSlow : public Port8Bit
{
    public:
    Port8BitSlow(uint16_t portnumber);
    ~Port8BitSlow();

    void Write(uint8_t data);
};

class Port16Bit : public Port
{
    public:
    Port16Bit(uint16_t portnumber);
    ~Port16Bit();

    void Write(uint16_t);
    uint16_t Read();
};

class Port32Bit : public Port
{
    public:
    Port32Bit(uint16_t portnumber);
    ~Port32Bit();

    void Write(uint32_t);
    uint32_t Read();
};

# endif