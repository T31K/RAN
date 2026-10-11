#pragma once

// Odyssey Rift - co-op wave survival mode (field server only).
// Design: docs/superpowers/specs/2026-10-09-odyssey-rift-mode-design.md
// Every position, mob id, price and zone comes from odysseyrift.ini next to the server exe,
// reloadable in game with the GM chat command "rift reload".

#include <map>
#include <set>
#include <string>
#include <vector>

#include "../[Lib]__Engine/Sources/G-Logic/GLDefine.h"

class GLChar;
class GLCrow;
class GLLandMan;
struct GLSKILL;

class GLOdysseyRift
{
public:
	enum EMSTATE { STATE_IDLE, STATE_CHARGING, STATE_WAVE, STATE_OVER };

	struct SBOX { float fX1, fZ1, fX2, fZ2; };	// axis-aligned room in world x/z

	struct SZONE
	{
		int			nID;
		float		fX, fZ, fRadius;	// circle in world x/z (radius 0 = boxes only; x/z = first box centre)
		std::vector<SBOX> vecBox;		// rooms that also belong to this zone
		DWORD		dwPrice;			// 0 = open from the start
		float		fSealX, fSealZ;		// where the seal marker stands
		bool		bOpen;
		DWORD		dwSealGlobID;
		SZONE () : nID(0), fX(0), fZ(0), fRadius(0), dwPrice(0), fSealX(0), fSealZ(0),
			bOpen(false), dwSealGlobID(UINT_MAX) {}
	};

	struct SSHOP
	{
		int			nZone;
		float		fX, fZ;
		std::string	strKind;			// heal | skills | overclock
		DWORD		dwPrice;
		int			nParam;
		std::string	strName;
	};

	struct SMOBDEF
	{
		SNATIVEID	sID;
		int			nTier;				// 1 common, 2 special, 3 boss
	};

	struct SPLAYER
	{
		DWORD		dwCharID;
		DWORD		dwEssence;
		DWORD		dwEarned;
		DWORD		dwKills;
		DWORD		dwSkillTier;		// highest skill learn level usable in the arena
		D3DXVECTOR3	vLastValid;
		bool		bCharged;
		float		fChargeTime;
		float		fWarnCool;
		SPLAYER () : dwCharID(0), dwEssence(0), dwEarned(0), dwKills(0), dwSkillTier(47),	// dwEssence unused: gold is the currency
			vLastValid(0,0,0), bCharged(false), fChargeTime(0), fWarnCool(0) {}
	};

	struct SLIVEMOB
	{
		DWORD		dwGlobID;
		SNATIVEID	sID;
		int			nTier;
	};

	struct SNPC
	{
		SNATIVEID	sID;
		float		fX, fZ;
		DWORD		dwGlobID;
	};

	struct SSPAWN
	{
		D3DXVECTOR3	vPos;
		int			nZone;
	};

public:
	static GLOdysseyRift& GetInstance ();

	bool LoadConfig ();							// reads odysseyrift.ini; false = mode off
	void FrameMove ( float fElapsed );			// every field-server frame
	BOOL OnChat ( GLChar* pChar, const char* szMsg );	// TRUE = message handled
	bool OnCrowKilled ( GLCrow* pCrow );		// a crow just reached 0 HP; true = rift mob (EXP only, no floor drops)
	bool CanUseSkill ( GLChar* pChar, const GLSKILL* pSkill );
	bool IsArenaLand ( const GLLandMan* pLand ) const;
	bool SuppressNaturalSpawns ( const GLLandMan* pLand ) const	{ return IsArenaLand ( pLand ); }

private:
	GLOdysseyRift ();

	GLLandMan* ArenaLand ();
	void Setup ( GLLandMan* pLand );
	bool OnNavi ( GLLandMan* pLand, float x, float z, D3DXVECTOR3& vOut );
	void SnapToNavi ( GLLandMan* pLand, float& fX, float& fZ );		// nearest walkable point within 400
	void SyncPlayers ( GLLandMan* pLand );
	void EnforceSeals ( GLLandMan* pLand, float fElapsed );
	void TickCharging ( float fElapsed );
	void TickWave ( GLLandMan* pLand, float fElapsed );
	void StartWave ();
	void EndRound ();
	void EndRun ();
	void ResetRun ( GLLandMan* pLand );
	void SpawnSeals ( GLLandMan* pLand );
	void HuntPlayers ( GLLandMan* pLand );
	bool SpawnOne ( GLLandMan* pLand, int nTier );
	void PruneMobs ( GLLandMan* pLand );
	bool SafeDropOut ( GLLandMan* pLand, DWORD dwGlobID, SNATIVEID sID );	// only if the slot still holds sID
	void DropAllLive ( GLLandMan* pLand );

	int ZoneOf ( const D3DXVECTOR3& vPos ) const;
	static bool ZoneHas ( const SZONE& z, const D3DXVECTOR3& vPos );
	bool InOpenZone ( const D3DXVECTOR3& vPos ) const;
	SPLAYER* FindPlayer ( GLChar* pChar );
	const char* Name ( DWORD dwGaeaID );

	void Jump ( GLChar* pChar, const D3DXVECTOR3& vPos );
	bool Recall ( GLChar* pChar, SNATIVEID sMap, const D3DXVECTOR3& vPos );

	void GiveGold ( GLChar* pChar, SPLAYER* pPlayer, LONGLONG lnGold );	// wallet + client update
	bool TakeGold ( GLChar* pChar, LONGLONG lnGold );					// false = not enough
	void GiveGoldAll ( LONGLONG lnGold );

	void Tell ( GLChar* pChar, const char* szFormat, ... );
	void Announce ( const char* szFormat, ... );

	void CmdBuy ( GLChar* pChar, SPLAYER* pPlayer );
	void CmdShop ( GLChar* pChar );
	BOOL CmdGM ( GLChar* pChar, const char* szArgs );

private:
	bool					m_bEnabled;
	bool					m_bSetup;
	SNATIVEID				m_sArenaMap;
	SNATIVEID				m_sEntryMap;
	D3DXVECTOR3				m_vEntryPos;
	float					m_fEntryRadius;
	SNATIVEID				m_sEntryNpc;
	DWORD					m_dwEntryNpcGlobID;
	D3DXVECTOR3				m_vNexus;
	float					m_fNexusRadius;
	SNATIVEID				m_sSealNpc;
	DWORD					m_dwStartEssence;	// unused since gold is the currency (kept for old configs)
	DWORD					m_dwGoldKill;		// gold per common kill, straight into the wallet
	DWORD					m_dwGoldRound;		// gold per voyager per survived leg

	std::vector<SMOBDEF>	m_vecMobs;
	std::vector<SZONE>		m_vecZones;
	std::vector<SSHOP>		m_vecShops;
	std::vector<SSPAWN>		m_vecSpawns;
	std::vector<SNPC>		m_vecNpcs;

	EMSTATE					m_emState;
	int						m_nRound;
	int						m_nToSpawn;			// mobs still to spawn this round
	int						m_nSpawned;
	float					m_fSpawnTimer;
	float					m_fHuntTimer;
	float					m_fStateTimer;
	bool					m_bWarnedLast;
	bool					m_bSecretIthaca;
	bool					m_bRoundDeath;
	bool					m_bMuseSang;
	int						m_nGolden;			// spawn index of this leg's golden soul, -1 none
	std::map<DWORD,SPLAYER>	m_mapPlayers;		// key: GaeaID
	std::map<DWORD,SPLAYER>	m_mapLeft;			// key: CharID - voyagers who stepped out this voyage
	std::map<DWORD,float>	m_mapFarTime;		// key: GlobID - seconds a rift mob spent far from everyone
	float					m_fWaveTime;
	std::vector<SLIVEMOB>	m_vecLive;
};
