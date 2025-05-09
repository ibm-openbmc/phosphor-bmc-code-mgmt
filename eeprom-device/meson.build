eeprom_device_version_src = files(
    'eeprom_device_version.cpp',
    'pt5161l/pt5161l.cpp',
)

eeprom_device_include = include_directories('.')

libgpiod = dependency(
    'libgpiodcxx',
    default_options: ['bindings=cxx'],
    version: '>=1.1.2',
)

executable(
    'phosphor-eepromdevice-software-update',
    'eeprom_device_software_manager.cpp',
    'eeprom_device.cpp',
    eeprom_device_version_src,
    include_directories: [common_include, eeprom_device_include],
    dependencies: [sdbusplus_dep, phosphor_logging_dep, libgpiod],
    link_with: [software_common_lib, libpldmutil],
    install: true,
    install_dir: get_option('libexecdir') / 'phosphor-code-mgmt',
)

systemd_system_unit_dir = dependency('systemd').get_variable(
    'systemdsystemunitdir',
    pkgconfig_define: ['prefix', get_option('prefix')],
)

install_data(
    'xyz.openbmc_project.Software.EEPROMDevice.service',
    install_dir: systemd_system_unit_dir,
)
