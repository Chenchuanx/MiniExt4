#include <drivers/driver.h>

Driver::Driver() { }
Driver::~Driver() { }
void Driver::Activate() { }
int Driver::Reset() { return 0; }
void Driver::Deactivate() { }

DriverManager::DriverManager() : numDrivers(0)
{
    for(uint16_t i = 0; i < 256; ++i)
    {
        drivers[i] == nullptr;
    }
}

DriverManager::~DriverManager() { }

void DriverManager::Activate()
{
    for(uint8_t i = 0; i < numDrivers; ++i)
    {
        drivers[i]->Activate();
    }
}

void DriverManager::AddDriver(Driver * driver)
{
    drivers[numDrivers] = driver;
    ++numDrivers;
}
