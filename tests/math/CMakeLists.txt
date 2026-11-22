# glm is the oracle the own types are checked against (glm_oracle.h); only this target links it directly.
aegis_add_test(
    math
    SOURCES
        math.test.cpp
        layout.test.cpp
        matrix.test.cpp
        quaternion.test.cpp
        scalar.test.cpp
        transform.test.cpp
        vector.test.cpp
    LIBS aegis-math glm::glm
)
