#include <gtest/gtest.h>
#include "all.h"
#include "../3rdParty/Storm/Source/storm.h"

TEST(Drlg_l1, DRLG_Init_Globals_4flag) {
	dvl::lightflag = false;
	dvl::light4flag = true;
	dvl::DRLG_Init_Globals();
	EXPECT_EQ(dvl::dLight[0][0], 3);
}

TEST(Drlg_l1, DRLG_Init_Globals_noflag) {
	dvl::lightflag = false;
	dvl::light4flag = false;
	dvl::DRLG_Init_Globals();
	EXPECT_EQ(dvl::dLight[0][0], 15);
}

TEST(Drlg_l1, DRLG_Init_Globals) {
	dvl::lightflag = true;
	dvl::DRLG_Init_Globals();
	EXPECT_EQ(dvl::dLight[0][0], 0);
}

void TestCreateL5Dungeon(bool hellfire, int level, int seed, int entry)
{
	dvl::pMegaTiles = new dvl::BYTE[1648*32];

	dvl::currlevel = level;
	dvl::leveltype = dvl::DTYPE_CATHEDRAL;

	dvl::CreateL5Dungeon(seed, entry);
	dvl::SFileSetBasePath("/home/ajenbo/code/diablo/devilutionX/test/fixtures/");
	char dunPath[128];
	if (hellfire)
		sprintf(dunPath, "hellfire/%d-%d.dun", level, seed);
	else
		sprintf(dunPath, "diablo/%d-%d.dun", level, seed);
	uint16_t *dunData = (uint16_t*)dvl::LoadFileInMem(dunPath, nullptr);
	ASSERT_EQ(40, dunData[0]);
	ASSERT_EQ(40, dunData[1]);

	const uint16_t *tileLayer = &dunData[2];

	for (int y = 0; y < 40; y++) {
		for (int x = 0; x < 40; x++) {
			auto tileId = static_cast<uint8_t>(SDL_SwapLE16(*tileLayer));
			tileLayer++;
			EXPECT_EQ(dvl::dungeon[x][y], tileId) << "bad tile at " << x << "x" << y;
		}
	}

	const uint16_t *transparentLayer = &dunData[2 + DMAXX * DMAXY * 13];

	for (int y = 16; y < 16 + 40 * 2; y++) {
		for (int x = 16; x < 16 + 40 * 2; x++) {
			auto sectorId = static_cast<uint8_t>(SDL_SwapLE16(*transparentLayer));
			transparentLayer++;
			//ASSERT_EQ(dvl::dTransVal[x][y], sectorId);
		}
	}
}

TEST(Drlg_l1, CreateL5Dungeon_diablo_1_743271966)
{
	TestCreateL5Dungeon(false, 1, 743271966, 0);
	EXPECT_EQ(dvl::ViewX, 51);
	EXPECT_EQ(dvl::ViewY, 82);
	TestCreateL5Dungeon(false, 1, 743271966, 0);
	EXPECT_EQ(dvl::ViewX, 81);
	EXPECT_EQ(dvl::ViewY, 47);
}

TEST(Drlg_l1, CreateL5Dungeon_diablo_2_1383137027)
{
	dvl::quests[dvl::Q_PWATER]._qlevel = 2;
	dvl::quests[dvl::Q_PWATER]._qactive = dvl::QUEST_INIT;

	TestCreateL5Dungeon(false, 2, 1383137027, 0);
}

TEST(Drlg_l1, CreateL5Dungeon_diablo_3_844660068)
{
	TestCreateL5Dungeon(false, 3, 844660068, 0);
}

TEST(Drlg_l1, CreateL5Dungeon_diablo_4_609325643)
{
	TestCreateL5Dungeon(false, 4, 609325643, 0);
}

