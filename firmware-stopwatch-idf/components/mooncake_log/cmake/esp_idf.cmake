# Src files
file(GLOB_RECURSE MOONCAKE_LOG_SRCS
    ${MOONCAKE_LOG_ROOT_DIR}/src/*.c
    ${MOONCAKE_LOG_ROOT_DIR}/src/*.cc
    ${MOONCAKE_LOG_ROOT_DIR}/src/*.cpp
)
# Include
set(MOONCAKE_LOG_INCS
    ${MOONCAKE_LOG_ROOT_DIR}/src/
)

# Public component requirement
set(MOONCAKE_LOG_REQUIRES
)

# Private component requirement
set(MOONCAKE_LOG_PRIV_REQUIRES
)

# Register component
idf_component_register(
    SRCS ${MOONCAKE_LOG_SRCS}
    INCLUDE_DIRS ${MOONCAKE_LOG_INCS}
    REQUIRES ${MOONCAKE_LOG_REQUIRES}
    PRIV_REQUIRES ${MOONCAKE_LOG_PRIV_REQUIRES}
)