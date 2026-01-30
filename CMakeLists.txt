set(srcs
    "src/fram_crc.c"
    "src/fram_dev.c"
    "src/fram_partition.c"
    "src/fram_ring.c"
    "src/fram_vslot.c"
    "src/fram_superblock.c"
)

if(CONFIG_FRAM_KVS_ENABLED)
    list(APPEND srcs "src/fram_kvs.c")
endif()

if(CONFIG_FRAM_HAL_SPI_ENABLED)
    list(APPEND srcs "src/fram_hal_spi.c")
endif()

if(CONFIG_FRAM_HAL_MOCK_ENABLED)
    list(APPEND srcs "src/fram_hal_mock.c")
endif()

set(requires esp_timer freertos)
if(CONFIG_FRAM_HAL_SPI_ENABLED)
    list(APPEND requires driver)
endif()

set(priv_requires "")
if(NOT CONFIG_IDF_TARGET_LINUX)
    list(APPEND priv_requires esp_rom)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "src"
    REQUIRES
        ${requires}
    PRIV_REQUIRES
        ${priv_requires}
)
