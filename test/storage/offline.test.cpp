#include <mbgl/storage/offline.hpp>
#include <mbgl/tile/tile_id.hpp>

#include <gtest/gtest.h>

using namespace mbgl;
using SourceType = mbgl::style::SourceType;

static const LatLngBounds sanFrancisco =
    LatLngBounds::hull({ 37.6609, -122.5744 }, { 37.8271, -122.3204 });

static const LatLngBounds sanFranciscoWrapped =
    LatLngBounds::hull({ 37.6609, 238.5744 }, { 37.8271, 238.3204 });

TEST(OfflineRegionDefinition, TileCoverEmpty) {
    OfflineRegionDefinition region("", LatLngBounds::empty(), 0, 20, 1.0);

    EXPECT_EQ((std::vector<CanonicalTileID>{}), region.tileCover(SourceType::Vector, 512, { 0, 22 }));
}

TEST(OfflineRegionDefinition, TileCoverZoomIntersection) {
    OfflineRegionDefinition region("", sanFrancisco, 2, 2, 1.0);

    EXPECT_EQ((std::vector<CanonicalTileID>{ { 2, 0, 1 } }),
              region.tileCover(SourceType::Vector, 512, { 0, 22 }));

    EXPECT_EQ((std::vector<CanonicalTileID>{}), region.tileCover(SourceType::Vector, 512, { 3, 22 }));
}

TEST(OfflineRegionDefinition, TileCoverTileSize) {
    OfflineRegionDefinition region("", LatLngBounds::world(), 0, 0, 1.0);

    EXPECT_EQ((std::vector<CanonicalTileID>{ { 0, 0, 0 } }),
              region.tileCover(SourceType::Vector, 512, { 0, 22 }));

    EXPECT_EQ((std::vector<CanonicalTileID>{ { 1, 0, 0 }, { 1, 0, 1 }, { 1, 1, 0 }, { 1, 1, 1 } }),
              region.tileCover(SourceType::Vector, 256, { 0, 22 }));
}

TEST(OfflineRegionDefinition, TileCoverZoomRounding) {
    OfflineRegionDefinition region("", sanFrancisco, 0.6, 0.7, 1.0);

    EXPECT_EQ((std::vector<CanonicalTileID>{ { 0, 0, 0 } }),
              region.tileCover(SourceType::Vector, 512, { 0, 22 }));

    EXPECT_EQ((std::vector<CanonicalTileID>{ { 1, 0, 0 } }),
              region.tileCover(SourceType::Raster, 512, { 0, 22 }));
}

TEST(OfflineRegionDefinition, TileCoverWrapped) {
    OfflineRegionDefinition region("", sanFranciscoWrapped, 0, 0, 1.0);

    EXPECT_EQ((std::vector<CanonicalTileID>{ { 0, 0, 0 } }),
              region.tileCover(SourceType::Vector, 512, { 0, 22 }));
}

TEST(OfflineRegionDefinition, TileCount) {
    OfflineRegionDefinition region("", sanFranciscoWrapped, 0, 22, 1.0);

    //These numbers match the count from tileCover().size().
    EXPECT_EQ(38424u, region.tileCount(SourceType::Vector, 512, { 10, 18 }));
    EXPECT_EQ(9675240u, region.tileCount(SourceType::Vector, 512, { 3, 22 }));
}

TEST(OfflineRegionDefinition, Point) {
    OfflineRegionDefinition region("", Point<double>(-122.5744, 37.6609), 0, 2, 1.0);

    EXPECT_EQ((std::vector<CanonicalTileID>{ { 0, 0, 0 }, { 1, 0, 0 }, {2 ,0 ,1 } }),
              region.tileCover(SourceType::Vector, 512, { 0, 22 }));
}

TEST(OfflineRegionDefinition, MultiPoint) {
    OfflineRegionDefinition region("", MultiPoint<double>{ { -122.5, 37.76 }, { -122.4, 37.76} }, 19, 20, 1.0);

    EXPECT_EQ((std::vector<CanonicalTileID>{
            {19, 83740, 202675}, {19, 83886, 202675},
            {20, 167480, 405351}, {20, 167772, 405351} }),
              region.tileCover(SourceType::Vector, 512, { 0, 22 }));
}

TEST(OfflineRegionDefinition, LineString) {
    OfflineRegionDefinition region("", LineString<double>{{ -122.5, 37.76 }, { -122.4, 37.76} }, 11, 14, 1.0);

    EXPECT_EQ((std::vector<CanonicalTileID>{
            {11, 327, 791},
            {12, 654, 1583}, {12, 655, 1583},
            {13, 1308, 3166}, {13, 1309, 3166}, {13, 1310, 3166},
            {14, 2616, 6333}, {14, 2617, 6333}, {14, 2618, 6333}, {14, 2619, 6333}, {14, 2620, 6333}, {14, 2621, 6333} }),
              region.tileCover(SourceType::Vector, 512, { 0, 22 }));
}

TEST(OfflineRegionDefinition, MultiLineString) {
    OfflineRegionDefinition region("", MultiLineString<double>{
            { { -122.5, 37.76 }, { -122.4, 37.76} },
            { { -122.5, 37.72 }, { -122.4, 37.72} } },
            13, 14, 1.0);

    EXPECT_EQ((std::vector<CanonicalTileID>{
            {11, 327, 791},
            {12, 654, 1583}, {12, 655, 1583},
            {13, 1308, 3166}, {13, 1309, 3166}, {13, 1310, 3166},
            {14, 2616, 6333}, {14, 2617, 6333}, {14, 2618, 6333}, {14, 2619, 6333}, {14, 2620, 6333}, {14, 2621, 6333} }),
              region.tileCover(SourceType::Vector, 512, { 0, 22 }));
}
