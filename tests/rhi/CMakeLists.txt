aegis_add_test(rhi
    SOURCES
        deletion_queue.test.cpp
        free_list.test.cpp
        query.test.cpp
        image.test.cpp
        image_view.test.cpp
        resource_pool.test.cpp
        ring_allocator.test.cpp
        sampler.test.cpp
        swapchain.test.cpp
        upload_manager.test.cpp
        utility.test.cpp
    LIBS aegis-rhi
)
