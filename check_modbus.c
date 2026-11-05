#include <stdio.h>
#include <modbus.h>

int main(void)
{
    printf("libmodbus %u.%u.%u\n",
           libmodbus_version_major,
           libmodbus_version_minor,
           libmodbus_version_micro);
    return 0;
}
