## for work with GPIO
http://www.orangepi.org/html/hardWare/computerAndMicrocontrollers/details/orange-pi-3-LTS.html
https://linux-sunxi.org/1-Wire
https://www.raspberrypi-spy.co.uk/2013/03/raspberry-pi-1-wire-digital-thermometer-sensor/
http://mypractic.ru/ds18b20-datchik-temperatury-s-interfejsom-1-wire-opisanie-na-russkom-yazyke.html
https://www.analog.com/media/en/technical-documentation/data-sheets/DS18B20.pdf
https://orangepi.su/content.php?p=194&chpu=Stavim%20datchik%20temperatury%20k%20Orange%20Pi%20/%20Raspberry%20Pi
https://github.com/leech001/Temp_control
https://f1atb.fr/temperature-ds18b20-and-orange-pi-zero/



>sudo nano /boot/orangepiEnv.txt
```bush
overlays=w1-gpio uart3 i2c0 i2c1
#if theree exist i2c then overlays=w1-gpio i2c0 i2c1 ...
#if theree exist serial then overlays=uart1 ...
param_w1_pin=PD22
param_w1_pin_int_pullup=1
```

or

>sudo orangepi-config > System > Bootenv

>sudo reboot

>sudo modprobe w1-gpio

>sudo modprobe w1-therm

//gpio mode 2 out ?

>gpio readall
```bash
 +------+-----+----------+--------+---+   OPi 3  +---+--------+----------+-----+------+
 | GPIO | wPi |   Name   |  Mode  | V | Physical | V |  Mode  | Name     | wPi | GPIO |
 +------+-----+----------+--------+---+----++----+---+--------+----------+-----+------+
 |      |     |     3.3V |        |   |  1 || 2  |   |        | 5V       |     |      |
 |  122 |   0 |    SDA.0 |   ALT2 | 0 |  3 || 4  |   |        | 5V       |     |      |
 |  121 |   1 |    SCL.0 |   ALT2 | 0 |  5 || 6  |   |        | GND      |     |      |
 |  118 |   2 |    PWM.0 |     IN | 1 |  7 || 8  | 0 | OFF    | PL02     | 3   | 354  |
 |      |     |      GND |        |   |  9 || 10 | 0 | OFF    | PL03     | 4   | 355  |
 |  120 |   5 |    RXD.3 |   ALT4 | 0 | 11 || 12 | 0 | OFF    | PD18     | 6   | 114  |
 |  119 |   7 |    TXD.3 |   ALT4 | 0 | 13 || 14 |   |        | GND      |     |      |
 |  362 |   8 |     PL10 |    OFF | 0 | 15 || 16 | 0 | OFF    | PD15     | 9   | 111  |
 |      |     |     3.3V |        |   | 17 || 18 | 0 | OFF    | PD16     | 10  | 112  |
 |  229 |  11 |   MOSI.1 |   ALT4 | 0 | 19 || 20 |   |        | GND      |     |      |
 |  230 |  12 |   MISO.1 |   ALT4 | 0 | 21 || 22 | 0 | OFF    | PD21     | 13  | 117  |
 |  228 |  14 |   SCLK.1 |    OFF | 0 | 23 || 24 | 0 | OFF    | CE.1     | 15  | 227  |
 |      |     |      GND |        |   | 25 || 26 | 0 | OFF    | PL08     | 16  | 360  |
 +------+-----+----------+--------+---+----++----+---+--------+----------+-----+------+
 | GPIO | wPi |   Name   |  Mode  | V | Physical | V |  Mode  | Name     | wPi | GPIO |
 +------+-----+----------+--------+---+   OPi 3  +---+--------+----------+-----+------+
```

>lsmod | grep w1
```bash
w1_therm               28672  0
w1_gpio                16384  0
wire                   36864  2 w1_gpio,w1_therm
```

>ls /sys/bus/w1/devices/
```
w1_bus_master1  28-XXXXXXXXXXXX
```



![Rectangle 741](https://github.com/user-attachments/assets/9c7eace9-507e-4ed3-bf48-2cedfa4ee452)
![Rectangle 741](https://github.com/user-attachments/assets/9022c55d-a769-40f4-890f-462ed39d795b)

## for test Temp Module!  

|ID|For                        | Name |
|--|--                        | --|
|28-030397941733| test 1 | test temp|
|28-0303979402d4| main | temp|
|28-030397942cf4| secondory|temp2|
|28-030397946349| input Bake |inBake|
|28-04175013faff| Output Bake | outBake|

>lsmod | grep w1
```bash
w1_therm               28672  0
w1_gpio                16384  0
wire                   36864  2 w1_gpio,w1_therm
```

>ls /sys/bus/w1/devices/
```
w1_bus_master1  28-XXXXXXXXXXXX
```

--------
