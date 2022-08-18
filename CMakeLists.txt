if(CONFIG_LCZ_LWM2M_FILL_LEVEL)
    zephyr_include_directories(include)
    zephyr_sources(source/lcz_lwm2m_fill_level.c)
endif()