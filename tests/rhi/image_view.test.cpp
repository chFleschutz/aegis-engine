#include <doctest/doctest.h>

import aegis.rhi;

TEST_SUITE("rhi::defaultViewType")
{
    using aegis::rhi::ImageType;
    using aegis::rhi::ImageViewType;
    using aegis::rhi::detail::defaultViewType;

    TEST_CASE("a single-layer view keeps the image's dimensionality")
    {
        CHECK(defaultViewType(ImageType::Dim1D, 1) == ImageViewType::Dim1D);
        CHECK(defaultViewType(ImageType::Dim2D, 1) == ImageViewType::Dim2D);
        CHECK(defaultViewType(ImageType::Dim3D, 1) == ImageViewType::Dim3D);
    }

    TEST_CASE("a view over several layers is an array of the image's dimensionality")
    {
        CHECK(defaultViewType(ImageType::Dim1D, 4) == ImageViewType::Dim1DArray);
        CHECK(defaultViewType(ImageType::Dim2D, 4) == ImageViewType::Dim2DArray);
    }

    TEST_CASE("a 2D image with 6 layers is an array, not a cube")
    {
        // Cube-ness comes from ImageType::Cube only; a layer count of 6 alone must not imply it.
        CHECK(defaultViewType(ImageType::Dim2D, 6) == ImageViewType::Dim2DArray);
    }

    TEST_CASE("a cube image viewed whole is a cube, viewed over several cubes a cube array")
    {
        CHECK(defaultViewType(ImageType::Cube, 6) == ImageViewType::Cube);
        CHECK(defaultViewType(ImageType::Cube, 12) == ImageViewType::CubeArray);
    }

    TEST_CASE("a cube image viewed through fewer faces is 2D")
    {
        CHECK(defaultViewType(ImageType::Cube, 1) == ImageViewType::Dim2D);
        CHECK(defaultViewType(ImageType::Cube, 3) == ImageViewType::Dim2DArray);
    }
}
