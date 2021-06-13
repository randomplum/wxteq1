project('wx-test', 'cpp', default_options: ['cpp_std=c++17', 'build.cpp_std=c++17'])

wxdep = dependency('wxwidgets', version: '>=3.0.0')
usbdep = dependency('libusb-1.0', version: '>=1.0.0')
xtensordep = dependency('xtensor', version: '>=0.20.0')
executable('wx-test', ['main.cpp', 'ThermalExpert.cpp'], dependencies : [wxdep, usbdep, xtensordep])
