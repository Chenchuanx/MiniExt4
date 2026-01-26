#ifndef __DRIVER_H_
#define __DRIVER_H_

#include <linux/types.h>

class Driver
{
public:
    Driver();
    ~Driver();

    virtual void Activate();
    virtual int Reset();
    virtual void Deactivate();
};

class DriverManager
{
public:
    DriverManager();
    ~DriverManager();

    void Activate();
    void AddDriver(Driver * driver);
private:
    Driver * drivers[256];
    int numDrivers;
};


#endif
