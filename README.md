# Socket offloading (aka modem driver) for SARA N2 modules

Socket offloading is (relatively) straight forward but requires some extra
care. For the AQ project we are using an UART extender running on I2C so there
are a few extra hoops and loops we have to go through when talking to the module.

The socket offloading is suitable for UDP data through N2 modules.

N3 modules use different AT commands (AT+NSOCR for N2, AT+USOCR for N3) but should
be relatively simple to implement.

