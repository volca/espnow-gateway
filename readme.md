# ESPNow gateway #

This project implements an ESP32-based ESPNow gateway, and its special feature is:

* It uses only a single ESP32 module, but connects to a wired network using either the IP101 or LAN8742. 
* It receives ESPNow broadcast packets and can parse sensor data using the BTHome protocol. 
* It automatically registers a component with Home Assistant and updates the sensor data.

这个项目实现了一个基于esp32的espnow网关，它的特别之处在于

* 仅用到了一个esp32模块，但是使用IP101或者LAN8742连接有线网络
* 接收ESPNow广播包，并可以用BTHome协议来解析传感器数据
* 自动向homeassistant注册component，并更新传感器数据

## TODO ##

Supported sensor types. 支持的传感器类型:

- [x] Button
- [ ] Temperature and humidity sensor

### Credits ###

The repo uses the following open source components:

* [bthome_base](https://github.com/afarago/esphome_component_bthome/tree/master/components/bthome_base)
