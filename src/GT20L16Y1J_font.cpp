#include "mbed.h"
#include "GT20L16J1Y_font.h"

#if defined(TARGET_LPC1768)
GT20L16J1Y_FONT::GT20L16J1Y_FONT() : _spi(p11, p12, p13), _CS(p10) {
}
#endif

GT20L16J1Y_FONT::GT20L16J1Y_FONT(PinName mosi, PinName miso, PinName sclk, PinName cs) : _spi(mosi, miso, sclk, NC), _CS(cs)
{
    // Setup the spi for 8 bit data, high steady state clock
    _spi.format(8,3);
    _spi.frequency(10000000);
}   

int GT20L16J1Y_FONT::read_kuten(unsigned short code) {
    unsigned char MSB, LSB;
    uint32_t address;
    int ret;
    
    MSB = (code & 0xFF00) >> 8;
    LSB = code & 0x00FF;
    address = 0;
    
    if(     MSB >=  1 && MSB <= 15 && LSB >= 1 && LSB <= 94)
        address =( (MSB -  1) * 94 + (LSB - 1))*32;
    else if(MSB >= 16 && MSB <= 47 && LSB >= 1 && LSB <= 94)
        address =( (MSB - 16) * 94 + (LSB - 1))*32 + 0x0AA40L;
    else if(MSB >= 48 && MSB <= 84 && LSB >= 1 && LSB <= 94)
        address = ((MSB - 48) * 94 + (LSB - 1))*32 + 0x21CDFL;
    else if(MSB == 85 &&                LSB >= 1 && LSB <= 94)
        address = ((MSB - 85) * 94 + (LSB - 1))*32 + 0x3C4A0L;
    else if(MSB >= 88 && MSB <= 89 && LSB >= 1 && LSB <= 94)
        address = ((MSB - 88) * 94 + (LSB - 1))*32 + 0x3D060L;
    else if(MSB == 0 && LSB >= 0x20 && LSB <= 0x7F)
        address = (LSB - 0x20)*16 + 255968;
    
    // Deselect the device
    _CS = 1;
    
    // Select the device by seting chip select low
    _CS = 0;
    _spi.write(0x03);    // Read data byte
    _spi.write(address>>16 & 0xff);
    _spi.write(address>>8 & 0xff);
    _spi.write(address & 0xff);
    
    if(MSB == 0 && LSB >= 0x20 && LSB <= 0x7F) {
        for(int i=0; i<16; i++)
        {
            bitmap[i] = _spi.write(0x00);
        }
        ret = 8;
    }
    else {
        for(int i=0; i<32; i++)
        {
            bitmap[i] = _spi.write(0x00);
        }
        ret = 16;
    }

    // Deselect the device
    _CS = 1;
    
    return ret;
}

void GT20L16J1Y_FONT::read(unsigned short code) {
    unsigned char c1, c2, MSB, LSB;
    uint32_t seq;
    uint16_t address;
    
    // SJIS to kuten code conversion
    c1 = (code>>8);
    c2 = (code & 0xFF);
    seq = (c1<=159 ? c1-129 : c1-193)*188 + (c2<=126 ? c2-64 : c2-65);
    MSB = seq / 94 + 1;
    LSB = seq % 94 + 1;
    
    address = ((MSB << 8) | LSB);
    read_kuten(address);
}
