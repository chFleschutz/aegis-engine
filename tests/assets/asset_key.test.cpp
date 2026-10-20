#include <doctest/doctest.h>

import std;
import aegis.assets;

using aegis::assets::AssetKey;

TEST_SUITE("assets::AssetKey")
{
    TEST_CASE("is equal when path and sub-index are equal")
    {
        CHECK(AssetKey{ "models/helmet.gltf", 2 } == AssetKey{ "models/helmet.gltf", 2 });
        CHECK(AssetKey{ "models/helmet.gltf", 2 } != AssetKey{ "models/helmet.gltf", 3 });
        CHECK(AssetKey{ "models/helmet.gltf", 2 } != AssetKey{ "models/Helmet.gltf", 2 });
        CHECK(AssetKey{ "a.png" } == AssetKey{ "a.png", 0 });
    }

    TEST_CASE("hashes equal keys equally and tells nearby keys apart")
    {
        const std::hash<AssetKey> hash;
        CHECK(hash({ "a/b.gltf", 1 }) == hash({ "a/b.gltf", 1 }));
        CHECK(hash({ "a/b.gltf", 1 }) != hash({ "a/b.gltf", 2 }));
        CHECK(hash({ "a/b.gltf", 1 }) != hash({ "a/c.gltf", 1 }));
    }

    TEST_CASE("works as a key of an unordered container")
    {
        std::unordered_map<AssetKey, int> map;
        map[{ "a.gltf", 0 }] = 1;
        map[{ "a.gltf", 1 }] = 2;
        map[{ "a.gltf", 0 }] = 3;
        CHECK(map.size() == 2);
        CHECK(map.at({ "a.gltf", 0 }) == 3);
        CHECK(map.at({ "a.gltf", 1 }) == 2);
    }
}
