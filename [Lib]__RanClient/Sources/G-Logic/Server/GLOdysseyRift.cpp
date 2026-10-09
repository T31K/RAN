#include "pch.h"
#include "./GLOdysseyRift.h"
#include "./GLGaeaServer.h"
#include "./GLLandMan.h"
#include "./GLChar.h"
#include "./GLCrow.h"
#include "../GLMobSchedule.h"
#include "../Data/GLSkill.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	const float	SPAWN_INTERVAL	= 1.5f;
	const float	HUNT_INTERVAL	= 2.0f;
	const float	CHARGE_HOLD		= 3.0f;
	const float	OVER_TIME		= 15.0f;
	const float	BUY_RANGE		= 120.0f;
	const DWORD	KILL_ESSENCE	= 50;
	const DWORD	ROUND_ESSENCE	= 250;

	float Dist2D ( const D3DXVECTOR3& a, float x, float z )
	{
		const float dx = a.x - x, dz = a.z - z;
		return sqrtf ( dx*dx + dz*dz );
	}

	bool IsAlive ( GLChar* pChar )	{ return pChar && pChar->IsValidBody() ? true : false; }
}

GLOdysseyRift& GLOdysseyRift::GetInstance ()
{
	static GLOdysseyRift Instance;
	return Instance;
}

GLOdysseyRift::GLOdysseyRift ()
	: m_bEnabled(false)
	, m_bSetup(false)
	, m_sArenaMap(43,0)
	, m_sEntryMap(5,0)
	, m_vEntryPos(0,0,0)
	, m_fEntryRadius(400.0f)
	, m_sEntryNpc(false)
	, m_dwEntryNpcGlobID(UINT_MAX)
	, m_vNexus(0,0,0)
	, m_fNexusRadius(150.0f)
	, m_sSealNpc(false)
	, m_dwStartEssence(500)
	, m_emState(STATE_IDLE)
	, m_nRound(0)
	, m_nToSpawn(0)
	, m_nSpawned(0)
	, m_fSpawnTimer(0)
	, m_fHuntTimer(0)
	, m_fStateTimer(0)
	, m_bWarnedLast(false)
	, m_bSecretIthaca(false)
	, m_bRoundDeath(false)
	, m_bMuseSang(false)
	, m_nGolden(-1)
	, m_fWaveTime(0)
{
}

// --------------------------------------------------------------------------- config

bool GLOdysseyRift::LoadConfig ()
{
	char szPath[MAX_PATH] = "";
	::GetModuleFileNameA ( NULL, szPath, MAX_PATH );
	char* pSlash = strrchr ( szPath, '\\' );
	if ( !pSlash ) pSlash = strrchr ( szPath, '/' );
	if ( pSlash ) *(pSlash+1) = 0; else szPath[0] = 0;
	StringCchCatA ( szPath, MAX_PATH, "odysseyrift.ini" );

	FILE* fp = fopen ( szPath, "rt" );
	if ( !fp )
	{
		m_bEnabled = false;
		CDebugSet::ToLogFile ( "[RIFT] no config at %s - mode off", szPath );
		return false;
	}

	m_vecMobs.clear(); m_vecZones.clear(); m_vecShops.clear(); m_vecNpcs.clear();
	m_bEnabled = false;

	char szLine[512];
	while ( fgets ( szLine, sizeof(szLine), fp ) )
	{
		char* pHash = strchr ( szLine, '#' );
		if ( pHash ) *pHash = 0;
		char szKey[64] = "";
		if ( sscanf ( szLine, " %63[a-z_] =", szKey ) != 1 ) continue;
		const char* pVal = strchr ( szLine, '=' );
		if ( !pVal ) continue;
		++pVal;

		int a=0, b=0, c=0; float x=0, z=0, r=0, sx=0, sz=0;
		std::string strKey = szKey;
		if ( strKey == "enabled" )				{ m_bEnabled = atoi ( pVal ) != 0; }
		else if ( strKey == "arena_map" && sscanf ( pVal, "%d %d", &a, &b ) == 2 )	m_sArenaMap = SNATIVEID ( (WORD)a, (WORD)b );
		else if ( strKey == "entry_map" && sscanf ( pVal, "%d %d", &a, &b ) == 2 )	m_sEntryMap = SNATIVEID ( (WORD)a, (WORD)b );
		else if ( strKey == "entry_pos" && sscanf ( pVal, "%f %f", &x, &z ) == 2 )	m_vEntryPos = D3DXVECTOR3 ( x, 0, z );
		else if ( strKey == "entry_radius" )	m_fEntryRadius = (float) atof ( pVal );
		else if ( strKey == "entry_npc" && sscanf ( pVal, "%d %d", &a, &b ) == 2 )	m_sEntryNpc = SNATIVEID ( (WORD)a, (WORD)b );
		else if ( strKey == "nexus_pos" && sscanf ( pVal, "%f %f", &x, &z ) == 2 )	m_vNexus = D3DXVECTOR3 ( x, 0, z );
		else if ( strKey == "nexus_radius" )	m_fNexusRadius = (float) atof ( pVal );
		else if ( strKey == "seal_npc" && sscanf ( pVal, "%d %d", &a, &b ) == 2 )	m_sSealNpc = SNATIVEID ( (WORD)a, (WORD)b );
		else if ( strKey == "start_essence" )	m_dwStartEssence = (DWORD) atoi ( pVal );
		else if ( strKey == "mob" && sscanf ( pVal, "%d %d %d", &a, &b, &c ) == 3 )
		{
			SMOBDEF sMob; sMob.sID = SNATIVEID ( (WORD)a, (WORD)b ); sMob.nTier = c;
			m_vecMobs.push_back ( sMob );
		}
		else if ( strKey == "zone" )
		{
			unsigned int nPrice = 0;
			if ( sscanf ( pVal, "%d %f %f %f %u %f %f", &a, &x, &z, &r, &nPrice, &sx, &sz ) >= 5 )
			{
				SZONE sZone; sZone.nID = a; sZone.fX = x; sZone.fZ = z; sZone.fRadius = r;
				sZone.dwPrice = nPrice; sZone.fSealX = sx; sZone.fSealZ = sz;
				sZone.bOpen = ( nPrice == 0 );
				m_vecZones.push_back ( sZone );
			}
		}
		else if ( strKey == "zonebox" )
		{
			// zonebox = id x1 z1 x2 z2 price seal_x seal_z ; repeat an id to add more rooms to it
			float x1=0, z1=0, x2=0, z2=0; unsigned int nPrice = 0;
			if ( sscanf ( pVal, "%d %f %f %f %f %u %f %f", &a, &x1, &z1, &x2, &z2, &nPrice, &sx, &sz ) >= 6 )
			{
				SBOX sBox; sBox.fX1 = x1 < x2 ? x1 : x2; sBox.fX2 = x1 < x2 ? x2 : x1;
				sBox.fZ1 = z1 < z2 ? z1 : z2; sBox.fZ2 = z1 < z2 ? z2 : z1;
				SZONE* pZone = NULL;
				for ( size_t i=0; i<m_vecZones.size(); ++i ) if ( m_vecZones[i].nID == a ) pZone = &m_vecZones[i];
				if ( !pZone )
				{
					SZONE sZone; sZone.nID = a; sZone.fRadius = 0;
					sZone.fX = (sBox.fX1+sBox.fX2)*0.5f; sZone.fZ = (sBox.fZ1+sBox.fZ2)*0.5f;
					sZone.dwPrice = nPrice; sZone.fSealX = sx; sZone.fSealZ = sz;
					sZone.bOpen = ( nPrice == 0 );
					m_vecZones.push_back ( sZone );
					pZone = &m_vecZones.back();
				}
				pZone->vecBox.push_back ( sBox );
			}
		}
		else if ( strKey == "npc" && sscanf ( pVal, "%d %d %f %f", &a, &b, &x, &z ) == 4 )
		{
			// npc = mid sid x z : a vendor / terminal standing in the arena (click = its normal shop)
			SNPC sNpc; sNpc.sID = SNATIVEID ( (WORD)a, (WORD)b ); sNpc.fX = x; sNpc.fZ = z; sNpc.dwGlobID = UINT_MAX;
			m_vecNpcs.push_back ( sNpc );
		}
		else if ( strKey == "shop" )
		{
			char szKind[32] = "", szName[64] = "";
			unsigned int nPrice = 0;
			if ( sscanf ( pVal, "%d %f %f %31s %u %d %63[^\r\n]", &a, &x, &z, szKind, &nPrice, &b, szName ) >= 6 )
			{
				SSHOP sShop; sShop.nZone = a; sShop.fX = x; sShop.fZ = z; sShop.strKind = szKind;
				sShop.dwPrice = nPrice; sShop.nParam = b; sShop.strName = szName[0] ? szName : szKind;
				m_vecShops.push_back ( sShop );
			}
		}
	}
	fclose ( fp );

	CDebugSet::ToLogFile ( "[RIFT] config %s: enabled=%d arena=%d/%d mobs=%d zones=%d shops=%d",
		szPath, m_bEnabled ? 1 : 0, m_sArenaMap.wMainID, m_sArenaMap.wSubID,
		(int) m_vecMobs.size(), (int) m_vecZones.size(), (int) m_vecShops.size() );
	return m_bEnabled;
}

bool GLOdysseyRift::IsArenaLand ( const GLLandMan* pLand ) const
{
	return m_bEnabled && pLand && pLand->GetMapID() == m_sArenaMap;
}

GLLandMan* GLOdysseyRift::ArenaLand ()
{
	return GLGaeaServer::GetInstance().GetByMapID ( m_sArenaMap );
}

// --------------------------------------------------------------------------- messages

void GLOdysseyRift::Tell ( GLChar* pChar, const char* szFormat, ... )
{
	if ( !pChar ) return;
	char szBuf[256];
	va_list ap; va_start ( ap, szFormat );
	StringCchVPrintfA ( szBuf, sizeof(szBuf), szFormat, ap );
	va_end ( ap );

	NET_CHAT_FB NetMsg;
	NetMsg.emType = CHAT_TYPE_NORMAL;
	StringCchCopy ( NetMsg.szName, CHR_ID_LENGTH+1, "Odyssey" );
	StringCchCopy ( NetMsg.szChatMsg, CHAT_MSG_SIZE+1, szBuf );
	GLGaeaServer::GetInstance().SENDTOCLIENT ( pChar->m_dwClientID, &NetMsg );
}

void GLOdysseyRift::Announce ( const char* szFormat, ... )
{
	char szBuf[256];
	va_list ap; va_start ( ap, szFormat );
	StringCchVPrintfA ( szBuf, sizeof(szBuf), szFormat, ap );
	va_end ( ap );

	NET_CHAT_FB NetMsg;
	NetMsg.emType = CHAT_TYPE_GLOBAL;
	StringCchCopy ( NetMsg.szName, CHR_ID_LENGTH+1, "" );
	StringCchCopy ( NetMsg.szChatMsg, CHAT_MSG_SIZE+1, szBuf );
	GLGaeaServer::GetInstance().SENDTOCLIENT_ONMAP ( m_sArenaMap.dwID, &NetMsg );
}

const char* GLOdysseyRift::Name ( DWORD dwGaeaID )
{
	PGLCHAR pChar = GLGaeaServer::GetInstance().GetChar ( dwGaeaID );
	return pChar ? pChar->m_szName : "?";
}

// --------------------------------------------------------------------------- movement

void GLOdysseyRift::Jump ( GLChar* pChar, const D3DXVECTOR3& vTarget )
{
	D3DXVECTOR3 vPos = vTarget;
	pChar->SetPosition ( vPos );
	pChar->ResetAction ();

	GLMSG::SNET_GM_MOVE2GATE_FB NetMsgFB;
	NetMsgFB.vPOS = pChar->m_vPos;
	GLGaeaServer::GetInstance().SENDTOCLIENT ( pChar->m_dwClientID, &NetMsgFB );

	GLMSG::SNETPC_JUMP_POS_BRD NetMsgBrd;
	NetMsgBrd.dwGaeaID = pChar->m_dwGaeaID;
	NetMsgBrd.vPOS = pChar->m_vPos;
	pChar->SendMsgViewAround ( (NET_MSG_GENERIC*) &NetMsgBrd );
}

bool GLOdysseyRift::Recall ( GLChar* pChar, SNATIVEID sMap, const D3DXVECTOR3& vPos )
{
	// Same field server only (this server hosts every map) - mirrors GMCtrolMove2MapPos.
	GLGaeaServer& gaea = GLGaeaServer::GetInstance();
	SNATIVEID sCurMap = pChar->m_sMapID;

	// Same cleanup as the bus (RequestBus): pets, summons and vehicles never follow a map move.
	gaea.DropOutPET ( pChar->m_dwPetGUID, false, true );
	gaea.DropOutSummon ( pChar->m_dwSummonGUID, false );
	gaea.SetActiveVehicle ( pChar->m_dwClientID, pChar->m_dwGaeaID, false );

	if ( !gaea.RequestInvenRecallThisSvr ( pChar, sMap, UINT_MAX, vPos ) )
		return false;

	if ( sCurMap != sMap )
	{
		for ( int i=0; i<EMBLOW_MULTI; ++i )		pChar->DISABLEBLOW ( i );
		for ( int i=0; i<SKILLFACT_SIZE; ++i )		pChar->DISABLESKEFF ( i );
	}
	pChar->ResetAction ();

	GLMSG::SNETPC_REQ_RECALL_FB NetMsgFB;
	NetMsgFB.emFB = EMREQ_RECALL_FB_OK;
	NetMsgFB.sMAPID = sMap;
	NetMsgFB.vPOS = pChar->m_vPos;
	gaea.SENDTOAGENT ( pChar->m_dwClientID, &NetMsgFB );
	return true;
}

// --------------------------------------------------------------------------- zones

bool GLOdysseyRift::ZoneHas ( const SZONE& z, const D3DXVECTOR3& vPos )
{
	if ( z.fRadius > 0 && Dist2D ( vPos, z.fX, z.fZ ) <= z.fRadius ) return true;
	for ( size_t i=0; i<z.vecBox.size(); ++i )
	{
		const SBOX& b = z.vecBox[i];
		if ( vPos.x >= b.fX1 && vPos.x <= b.fX2 && vPos.z >= b.fZ1 && vPos.z <= b.fZ2 ) return true;
	}
	return false;
}

int GLOdysseyRift::ZoneOf ( const D3DXVECTOR3& vPos ) const
{
	// The first zone (config order) that holds the point; concentric rings rely on this order.
	for ( size_t i=0; i<m_vecZones.size(); ++i )
		if ( ZoneHas ( m_vecZones[i], vPos ) ) return (int) i;

	int nBest = -1; float fBest = FLT_MAX;
	for ( size_t i=0; i<m_vecZones.size(); ++i )	// outside every zone: nearest centre
	{
		const float d = Dist2D ( vPos, m_vecZones[i].fX, m_vecZones[i].fZ ) - m_vecZones[i].fRadius;
		if ( d < fBest ) { fBest = d; nBest = (int) i; }
	}
	return nBest;
}

bool GLOdysseyRift::InOpenZone ( const D3DXVECTOR3& vPos ) const
{
	if ( m_vecZones.empty() ) return true;
	for ( size_t i=0; i<m_vecZones.size(); ++i )
	{
		const SZONE& z = m_vecZones[i];
		if ( z.bOpen && ZoneHas ( z, vPos ) ) return true;
	}
	return false;
}

// --------------------------------------------------------------------------- setup

bool GLOdysseyRift::OnNavi ( GLLandMan* pLand, float x, float z, D3DXVECTOR3& vOut )
{
	D3DXVECTOR3 vHit(0,0,0);
	if ( !pLand->IsCollisionNavi ( D3DXVECTOR3(x,+10000.0f,z), D3DXVECTOR3(x,-10000.0f,z), vHit ) ) return false;
	vOut = vHit;
	return true;
}

void GLOdysseyRift::SnapToNavi ( GLLandMan* pLand, float& fX, float& fZ )
{
	D3DXVECTOR3 vHit;
	if ( OnNavi ( pLand, fX, fZ, vHit ) ) return;
	const float x0 = fX, z0 = fZ;
	float fBest = FLT_MAX;
	for ( int k=0; k<800; ++k )
	{
		const float x = x0 + (float)( rand() % 800 - 400 ), z = z0 + (float)( rand() % 800 - 400 );
		const float d = (x-x0)*(x-x0) + (z-z0)*(z-z0);
		if ( d < fBest && OnNavi ( pLand, x, z, vHit ) ) { fBest = d; fX = vHit.x; fZ = vHit.z; }
	}
}

void GLOdysseyRift::Setup ( GLLandMan* pLand )
{
	m_bSetup = true;

	// Spawn points = the arena level's own mob-schedule positions (known walkable).
	m_vecSpawns.clear();
	std::set<DWORD> setNatural;
	GLMobScheduleMan* pSchMan = pLand->GetMobSchMan();
	if ( pSchMan )
	{
		MOBSCHEDULENODE* pNode = pSchMan->GetMobSchList()->m_pHead;
		for ( ; pNode; pNode = pNode->pNext )
		{
			GLMobSchedule* pSch = pNode->Data;
			if ( !pSch || !pSch->m_pAffineParts ) continue;
			SSPAWN sSpawn;
			sSpawn.vPos = pSch->m_pAffineParts->vTrans;
			sSpawn.nZone = ZoneOf ( sSpawn.vPos );
			m_vecSpawns.push_back ( sSpawn );
			setNatural.insert ( pSch->m_CrowID.dwID );
		}
	}

	// Arenas without schedules: sample the navigation mesh for walkable spawn points.
	D3DXVECTOR3 vMax(0,0,0), vMin(0,0,0);
	pLand->GetNaviMeshAABB ( vMax, vMin );
	if ( !OnNavi ( pLand, m_vNexus.x, m_vNexus.z, m_vNexus ) )
	{
		// The configured Nexus is off the mesh: use the walkable point nearest the map centre.
		const float cx = (vMax.x+vMin.x)*0.5f, cz = (vMax.z+vMin.z)*0.5f;
		D3DXVECTOR3 vHit; float fBest = FLT_MAX;
		for ( int i=0; i<3000; ++i )
		{
			const float x = vMin.x + (vMax.x-vMin.x) * (rand() / (float) RAND_MAX);
			const float z = vMin.z + (vMax.z-vMin.z) * (rand() / (float) RAND_MAX);
			const float d = (x-cx)*(x-cx) + (z-cz)*(z-cz);
			if ( d < fBest && OnNavi ( pLand, x, z, vHit ) ) { fBest = d; m_vNexus = vHit; }
		}
		CDebugSet::ToLogFile ( "[RIFT] nexus off-mesh, moved to %.0f %.0f", m_vNexus.x, m_vNexus.z );
	}
	// Seals are only safe if the Nexus (the push-back fallback) stands in a free zone.
	bool bNexusFree = false;
	for ( size_t i=0; i<m_vecZones.size(); ++i )
		if ( m_vecZones[i].dwPrice == 0 && ZoneHas ( m_vecZones[i], m_vNexus ) )
			bNexusFree = true;
	if ( !m_vecZones.empty() && !bNexusFree )
	{
		CDebugSet::ToLogFile ( "[RIFT] the Nexus is outside every free zone - seals disabled" );
		m_vecZones.clear();
	}

	// Seal markers and wares stand on walkable ground near their configured spot.
	for ( size_t i=0; i<m_vecZones.size(); ++i )
	{
		SZONE& z = m_vecZones[i];
		if ( z.dwPrice == 0 ) continue;
		SnapToNavi ( pLand, z.fSealX, z.fSealZ );
		CDebugSet::ToLogFile ( "[RIFT] seal %d at %.0f %.0f", z.nID, z.fSealX, z.fSealZ );
	}
	for ( size_t i=0; i<m_vecShops.size(); ++i )
	{
		SnapToNavi ( pLand, m_vecShops[i].fX, m_vecShops[i].fZ );
		CDebugSet::ToLogFile ( "[RIFT] ware %s at %.0f %.0f", m_vecShops[i].strName.c_str(), m_vecShops[i].fX, m_vecShops[i].fZ );
	}

	if ( m_vecSpawns.size() < 8 )
	{
		for ( int i=0; i<6000 && m_vecSpawns.size() < 80; ++i )
		{
			const float x = vMin.x + (vMax.x-vMin.x) * (rand() / (float) RAND_MAX);
			const float z = vMin.z + (vMax.z-vMin.z) * (rand() / (float) RAND_MAX);
			if ( Dist2D ( m_vNexus, x, z ) < 250.0f ) continue;	// never spawn on the Nexus
			SSPAWN sSpawn;
			if ( !OnNavi ( pLand, x, z, sSpawn.vPos ) ) continue;
			sSpawn.nZone = ZoneOf ( sSpawn.vPos );
			m_vecSpawns.push_back ( sSpawn );
		}
	}

	// No mob list in the config: the arena's own natives become the common tier.
	if ( m_vecMobs.empty() )
	{
		for ( std::set<DWORD>::iterator it = setNatural.begin(); it != setNatural.end(); ++it )
		{
			SMOBDEF sMob; sMob.sID.dwID = *it; sMob.nTier = 1;
			m_vecMobs.push_back ( sMob );
		}
	}

	// Remove the natural population that spawned when the map loaded.
	std::vector<DWORD> vecDrop;
	for ( DWORD i=0; i<MAXCROW; ++i )
		if ( pLand->GetCrow ( i ) ) vecDrop.push_back ( i );
	for ( size_t i=0; i<vecDrop.size(); ++i ) pLand->DropOutCrow ( vecDrop[i] );

	// Vendors and terminals inside the arena.
	for ( size_t i=0; i<m_vecNpcs.size(); ++i )
	{
		SnapToNavi ( pLand, m_vecNpcs[i].fX, m_vecNpcs[i].fZ );
		m_vecNpcs[i].dwGlobID = pLand->DropCrow ( m_vecNpcs[i].sID, m_vecNpcs[i].fX, m_vecNpcs[i].fZ );
		CDebugSet::ToLogFile ( "[RIFT] npc %d %d at %.0f %.0f -> %u", m_vecNpcs[i].sID.wMainID, m_vecNpcs[i].sID.wSubID,
			m_vecNpcs[i].fX, m_vecNpcs[i].fZ, m_vecNpcs[i].dwGlobID );
	}

	// The entry NPC in Mystic Peak Square.
	if ( m_sEntryNpc != SNATIVEID(false) )
	{
		GLLandMan* pEntry = GLGaeaServer::GetInstance().GetByMapID ( m_sEntryMap );
		if ( pEntry ) m_dwEntryNpcGlobID = pEntry->DropCrow ( m_sEntryNpc, m_vEntryPos.x, m_vEntryPos.z );
	}

	CDebugSet::ToLogFile ( "[RIFT] setup: %d spawn points, %d natives removed, %d mob types, entry npc %u",
		(int) m_vecSpawns.size(), (int) vecDrop.size(), (int) m_vecMobs.size(), m_dwEntryNpcGlobID );
}

void GLOdysseyRift::SpawnSeals ( GLLandMan* pLand )
{
	for ( size_t i=0; i<m_vecZones.size(); ++i )
	{
		SZONE& z = m_vecZones[i];
		if ( z.dwSealGlobID != UINT_MAX ) { SafeDropOut ( pLand, z.dwSealGlobID, m_sSealNpc ); z.dwSealGlobID = UINT_MAX; }
		z.bOpen = ( z.dwPrice == 0 );
		if ( !z.bOpen && m_sSealNpc != SNATIVEID(false) )
			z.dwSealGlobID = pLand->DropCrow ( m_sSealNpc, z.fSealX, z.fSealZ );
	}
}

// --------------------------------------------------------------------------- frame

void GLOdysseyRift::FrameMove ( float fElapsed )
{
	if ( !m_bEnabled ) return;
	GLLandMan* pLand = ArenaLand ();
	if ( !pLand ) return;
	if ( !m_bSetup ) Setup ( pLand );

	SyncPlayers ( pLand );
	PruneMobs ( pLand );

	if ( m_mapPlayers.empty() )
	{
		if ( m_emState != STATE_IDLE ) ResetRun ( pLand );
		return;
	}

	if ( m_emState == STATE_IDLE )
	{
		// A fresh voyage begins.
		m_emState = STATE_CHARGING;
		m_nRound = 0;
		m_bSecretIthaca = false;
		m_bMuseSang = false;
		m_mapLeft.clear();
		SpawnSeals ( pLand );
		for ( std::map<DWORD,SPLAYER>::iterator it = m_mapPlayers.begin(); it != m_mapPlayers.end(); ++it )
		{
			it->second.dwEssence = m_dwStartEssence; it->second.dwEarned = 0;
			it->second.dwKills = 0; it->second.dwSkillTier = 47; it->second.bCharged = false;
		}
		Announce ( "[NEXUS] The rift stirs. Gather at the Nexus to begin the Voyage." );
	}

	EnforceSeals ( pLand, fElapsed );

	switch ( m_emState )
	{
	case STATE_CHARGING:	TickCharging ( fElapsed );		break;
	case STATE_WAVE:		TickWave ( pLand, fElapsed );	break;
	case STATE_OVER:
		m_fStateTimer -= fElapsed;
		if ( m_fStateTimer <= 0 ) ResetRun ( pLand );
		break;
	default: break;
	}
}

void GLOdysseyRift::SyncPlayers ( GLLandMan* pLand )
{
	std::set<DWORD> setHere;
	for ( GLCHARNODE* pNode = pLand->m_GlobPCList.m_pHead; pNode; pNode = pNode->pNext )
	{
		GLChar* pChar = pNode->Data;
		if ( !pChar ) continue;
		setHere.insert ( pChar->m_dwGaeaID );

		std::map<DWORD,SPLAYER>::iterator it = m_mapPlayers.find ( pChar->m_dwGaeaID );
		if ( it == m_mapPlayers.end() || it->second.dwCharID != pChar->m_dwCharID )
		{
			// A voyager who left and came back keeps this voyage's Essence and skills.
			SPLAYER sPlayer;
			std::map<DWORD,SPLAYER>::iterator itOld = m_mapLeft.find ( pChar->m_dwCharID );
			if ( itOld != m_mapLeft.end() ) { sPlayer = itOld->second; m_mapLeft.erase ( itOld ); }
			else sPlayer.dwEssence = m_dwStartEssence;
			sPlayer.dwCharID = pChar->m_dwCharID;
			sPlayer.vLastValid = m_vNexus;
			sPlayer.bCharged = false;
			m_mapPlayers[pChar->m_dwGaeaID] = sPlayer;
			Tell ( pChar, "You step out of the Horse into another world." );
			Tell ( pChar, "Essence: %u. Type 'shop' for wares, 'buy' near one.", sPlayer.dwEssence );
			if ( m_emState == STATE_WAVE )
				Tell ( pChar, "Leg %d of the Voyage is under way. Fight!", m_nRound );
		}
		if ( m_emState == STATE_WAVE && !IsAlive ( pChar ) ) m_bRoundDeath = true;
	}

	for ( std::map<DWORD,SPLAYER>::iterator it = m_mapPlayers.begin(); it != m_mapPlayers.end(); )
	{
		if ( setHere.count ( it->first ) == 0 )
		{
			m_mapLeft[it->second.dwCharID] = it->second;
			m_mapPlayers.erase ( it++ );
		}
		else ++it;
	}
}

void GLOdysseyRift::EnforceSeals ( GLLandMan* pLand, float fElapsed )
{
	if ( m_vecZones.empty() ) return;
	for ( std::map<DWORD,SPLAYER>::iterator it = m_mapPlayers.begin(); it != m_mapPlayers.end(); ++it )
	{
		GLChar* pChar = GLGaeaServer::GetInstance().GetChar ( it->first );
		SPLAYER& sPlayer = it->second;
		if ( sPlayer.fWarnCool > 0 ) sPlayer.fWarnCool -= fElapsed;
		if ( !IsAlive ( pChar ) ) continue;

		if ( InOpenZone ( pChar->m_vPos ) ) { sPlayer.vLastValid = pChar->m_vPos; continue; }

		D3DXVECTOR3 vBack = InOpenZone ( sPlayer.vLastValid ) ? sPlayer.vLastValid : m_vNexus;
		Jump ( pChar, vBack );
		if ( sPlayer.fWarnCool <= 0 )
		{
			Tell ( pChar, "A Rift Seal repels you. Break it with Essence to pass." );
			sPlayer.fWarnCool = 3.0f;
		}
	}
}

void GLOdysseyRift::TickCharging ( float fElapsed )
{
	int nAlive = 0, nCharged = 0;
	for ( std::map<DWORD,SPLAYER>::iterator it = m_mapPlayers.begin(); it != m_mapPlayers.end(); ++it )
	{
		GLChar* pChar = GLGaeaServer::GetInstance().GetChar ( it->first );
		if ( !IsAlive ( pChar ) ) continue;
		++nAlive;
		const bool bIn = Dist2D ( pChar->m_vPos, m_vNexus.x, m_vNexus.z ) <= m_fNexusRadius;
		if ( bIn && !it->second.bCharged )
		{
			it->second.bCharged = true;
			int nNow = 0;
			for ( std::map<DWORD,SPLAYER>::iterator jt = m_mapPlayers.begin(); jt != m_mapPlayers.end(); ++jt )
				if ( jt->second.bCharged ) ++nNow;
			Announce ( "[NEXUS] %s charges the Nexus. %d / %d", pChar->m_szName, nNow, (int) m_mapPlayers.size() );
		}
		else if ( !bIn && it->second.bCharged )
		{
			it->second.bCharged = false;
		}
		if ( it->second.bCharged ) ++nCharged;
	}

	if ( nAlive > 0 && nCharged == nAlive )
	{
		m_fStateTimer += fElapsed;
		if ( m_fStateTimer >= CHARGE_HOLD ) StartWave ();
	}
	else m_fStateTimer = 0;
}

void GLOdysseyRift::StartWave ()
{
	++m_nRound;
	const int nPlayers = (int) m_mapPlayers.size();
	m_nToSpawn = 6 + 3*m_nRound + 2*(nPlayers-1);
	if ( m_nRound % 10 == 0 ) m_nToSpawn = 1 + m_nRound/2;	// boss + escorts
	m_nSpawned = 0;
	m_fSpawnTimer = 0;
	m_fHuntTimer = 0;
	m_fStateTimer = 0;
	m_fWaveTime = 0;
	m_bWarnedLast = false;
	m_bRoundDeath = false;
	m_emState = STATE_WAVE;
	for ( std::map<DWORD,SPLAYER>::iterator it = m_mapPlayers.begin(); it != m_mapPlayers.end(); ++it )
		it->second.bCharged = false;

	// Surprise: one in eight legs from leg 3 hides a golden soul in the horde.
	m_nGolden = ( m_nToSpawn > 0 && m_nRound >= 3 && m_nRound % 10 != 0 && rand() % 8 == 0 ) ? rand() % m_nToSpawn : -1;

	if ( m_nRound % 10 == 0 )
	{
		Announce ( "[NEXUS] The Nexus erupts! Leg %d - a CONVERGENCE. Something vast awakens.", m_nRound );
		Announce ( "[NEXUS] Stand together. Alone, you will not survive this." );
	}
	else if ( m_nRound % 5 == 0 )
		Announce ( "[NEXUS] Leg %d of the Voyage. The rift sends its hunters.", m_nRound );
	else
		Announce ( "[NEXUS] Leg %d of the Voyage. %d souls pour out of the rift!", m_nRound, m_nToSpawn );
}

bool GLOdysseyRift::SpawnOne ( GLLandMan* pLand, int nTier )
{
	std::vector<const SMOBDEF*> vecPick;
	for ( int t = nTier; t >= 1 && vecPick.empty(); --t )
		for ( size_t i=0; i<m_vecMobs.size(); ++i )
			if ( m_vecMobs[i].nTier == t ) vecPick.push_back ( &m_vecMobs[i] );
	if ( vecPick.empty() ) return false;

	// COD-style: the horde crawls in from the sealed rooms around the voyagers, never pops up
	// inside the ground they hold. Without zones, any spawn point will do.
	std::vector<const SSPAWN*> vecAt;
	for ( size_t i=0; i<m_vecSpawns.size(); ++i )
		if ( m_vecZones.empty() || !InOpenZone ( m_vecSpawns[i].vPos ) ) vecAt.push_back ( &m_vecSpawns[i] );
	if ( vecAt.empty() )
		for ( size_t i=0; i<m_vecSpawns.size(); ++i ) vecAt.push_back ( &m_vecSpawns[i] );
	if ( vecAt.empty() ) return false;

	const SMOBDEF* pMob = vecPick[ rand() % vecPick.size() ];
	const SSPAWN* pAt = vecAt[ rand() % vecAt.size() ];
	const float fJitterX = (float)( rand() % 120 - 60 ), fJitterZ = (float)( rand() % 120 - 60 );
	DWORD dwGlob = pLand->DropCrow ( pMob->sID, pAt->vPos.x + fJitterX, pAt->vPos.z + fJitterZ );
	if ( dwGlob == UINT_MAX ) dwGlob = pLand->DropCrow ( pMob->sID, pAt->vPos.x, pAt->vPos.z );
	if ( dwGlob == UINT_MAX ) return false;

	SLIVEMOB sLive; sLive.dwGlobID = dwGlob; sLive.sID = pMob->sID; sLive.nTier = pMob->nTier;
	m_vecLive.push_back ( sLive );
	return true;
}

void GLOdysseyRift::PruneMobs ( GLLandMan* pLand )
{
	for ( size_t i=0; i<m_vecLive.size(); )
	{
		GLCrow* pCrow = pLand->GetCrow ( m_vecLive[i].dwGlobID );
		if ( !pCrow || pCrow->m_sNativeID != m_vecLive[i].sID || !pCrow->IsValidBody() )
		{
			m_vecLive.erase ( m_vecLive.begin() + i );
		}
		else ++i;
	}
}

void GLOdysseyRift::HuntPlayers ( GLLandMan* pLand )
{
	for ( size_t i=0; i<m_vecLive.size(); ++i )
	{
		GLCrow* pCrow = pLand->GetCrow ( m_vecLive[i].dwGlobID );
		if ( !pCrow ) continue;
		GLChar* pBest = NULL; float fBest = FLT_MAX;
		for ( std::map<DWORD,SPLAYER>::iterator it = m_mapPlayers.begin(); it != m_mapPlayers.end(); ++it )
		{
			GLChar* pChar = GLGaeaServer::GetInstance().GetChar ( it->first );
			if ( !IsAlive ( pChar ) ) continue;
			const float d = Dist2D ( pChar->m_vPos, pCrow->GetPosition().x, pCrow->GetPosition().z );
			if ( d < fBest ) { fBest = d; pBest = pChar; }
		}
		if ( pBest ) pCrow->RiftHunt ( STARGETID ( CROW_PC, pBest->m_dwGaeaID, pBest->m_vPos ) );

		// A mob stranded far from every voyager (unreachable island) is pulled back into the rift
		// and re-rolled at a fresh spawn point, so no leg can stall on it.
		float& fFar = m_mapFarTime[m_vecLive[i].dwGlobID];
		fFar = ( fBest > 1500.0f ) ? fFar + HUNT_INTERVAL : 0.0f;
		if ( fFar >= 40.0f && m_vecLive[i].nTier != 3 )
		{
			SafeDropOut ( pLand, m_vecLive[i].dwGlobID, m_vecLive[i].sID );
			m_mapFarTime.erase ( m_vecLive[i].dwGlobID );
			m_vecLive.erase ( m_vecLive.begin() + i );
			--i;
			if ( m_nSpawned > 0 ) --m_nSpawned;
		}
	}
}

bool GLOdysseyRift::SafeDropOut ( GLLandMan* pLand, DWORD dwGlobID, SNATIVEID sID )
{
	if ( !pLand || dwGlobID == UINT_MAX ) return false;
	GLCrow* pCrow = pLand->GetCrow ( dwGlobID );
	if ( !pCrow || pCrow->m_sNativeID != sID ) return false;	// the slot holds someone else now
	return pLand->DropOutCrow ( dwGlobID ) ? true : false;
}

void GLOdysseyRift::DropAllLive ( GLLandMan* pLand )
{
	for ( size_t i=0; pLand && i<m_vecLive.size(); ++i ) SafeDropOut ( pLand, m_vecLive[i].dwGlobID, m_vecLive[i].sID );
	m_vecLive.clear();
	m_mapFarTime.clear();
}

void GLOdysseyRift::TickWave ( GLLandMan* pLand, float fElapsed )
{
	// Wipe: everyone in the arena is down.
	bool bAnyAlive = false;
	for ( std::map<DWORD,SPLAYER>::iterator it = m_mapPlayers.begin(); it != m_mapPlayers.end(); ++it )
		if ( IsAlive ( GLGaeaServer::GetInstance().GetChar ( it->first ) ) ) { bAnyAlive = true; break; }
	if ( !bAnyAlive ) { EndRun (); return; }

	const int nPlayers = (int) m_mapPlayers.size();
	const int nCap = ( 8*nPlayers < 30 ) ? 8*nPlayers : 30;
	m_fSpawnTimer -= fElapsed;
	if ( m_nSpawned < m_nToSpawn && (int) m_vecLive.size() < nCap && m_fSpawnTimer <= 0 )
	{
		int nTier = 1;
		if ( m_nRound % 10 == 0 )		nTier = ( m_nSpawned == 0 ) ? 3 : 1;
		else if ( m_nRound % 5 == 0 )	nTier = 2;
		else if ( m_nRound >= 3 && rand() % 4 == 0 ) nTier = 2;

		if ( SpawnOne ( pLand, nTier ) )
		{
			if ( m_nSpawned == m_nGolden )
			{
				m_vecLive.back().nTier = 9;	// golden soul
				Announce ( "[NEXUS] ...something golden flickers among the dead." );
			}
			++m_nSpawned;
		}
		else ++m_nSpawned;	// never stall a round on a failed spawn
		m_fSpawnTimer = SPAWN_INTERVAL;
	}

	m_fHuntTimer -= fElapsed;
	if ( m_fHuntTimer <= 0 ) { HuntPlayers ( pLand ); m_fHuntTimer = HUNT_INTERVAL; }

	// Safety net: no leg lasts forever, whatever the mobs got stuck on.
	m_fWaveTime += fElapsed;
	if ( m_fWaveTime > 180.0f + 6.0f * m_nToSpawn )
	{
		DropAllLive ( pLand );
		m_nSpawned = m_nToSpawn;
		Announce ( "[NEXUS] The rift collapses on the stragglers. The leg is yours." );
		EndRound ();
		return;
	}

	const int nLeft = ( m_nToSpawn - m_nSpawned ) + (int) m_vecLive.size();
	if ( nLeft <= 3 && nLeft > 0 && !m_bWarnedLast && m_nSpawned >= m_nToSpawn )
	{
		m_bWarnedLast = true;
		Announce ( "[NEXUS] %d left. The rift weakens - finish them!", nLeft );
	}
	if ( m_nSpawned >= m_nToSpawn && m_vecLive.empty() ) EndRound ();
}

void GLOdysseyRift::EndRound ()
{
	for ( std::map<DWORD,SPLAYER>::iterator it = m_mapPlayers.begin(); it != m_mapPlayers.end(); ++it )
	{
		it->second.dwEssence += ROUND_ESSENCE;
		it->second.dwEarned += ROUND_ESSENCE;
		it->second.bCharged = false;
	}
	m_emState = STATE_CHARGING;
	m_fStateTimer = 0;
	Announce ( "[NEXUS] Leg %d survived. +%u Essence each. The Nexus dims...", m_nRound, ROUND_ESSENCE );

	// Surprise: survive leg 7 with nobody falling and the muse sings.
	if ( m_nRound >= 7 && !m_bRoundDeath && !m_bMuseSang )
	{
		m_bMuseSang = true;
		Announce ( "[THE MUSE] Sing to me of the ones who never fell..." );
		for ( std::map<DWORD,SPLAYER>::iterator it = m_mapPlayers.begin(); it != m_mapPlayers.end(); ++it )
		{
			SPLAYER& s = it->second;
			s.dwSkillTier = s.dwSkillTier < 57 ? 57 : ( s.dwSkillTier < 67 ? 67 : 999 );
			s.dwEssence += 777; s.dwEarned += 777;
		}
		Announce ( "[THE MUSE] Your skills awaken one step further. +777 Essence." );
	}

	for ( std::map<DWORD,SPLAYER>::iterator it = m_mapPlayers.begin(); it != m_mapPlayers.end(); ++it )
	{
		GLChar* pChar = GLGaeaServer::GetInstance().GetChar ( it->first );
		Tell ( pChar, "Essence: %u. Spend it, then charge the Nexus together.", it->second.dwEssence );
	}
}

void GLOdysseyRift::EndRun ()
{
	GLLandMan* pLand = ArenaLand ();
	Announce ( "[NEXUS] The rift swallows the last of you. The Voyage ends at leg %d.", m_nRound );
	for ( std::map<DWORD,SPLAYER>::iterator it = m_mapPlayers.begin(); it != m_mapPlayers.end(); ++it )
		Announce ( "  %s - %u kills, %u Essence earned", Name ( it->first ), it->second.dwKills, it->second.dwEarned );
	Announce ( "[NEXUS] Revive and charge the Nexus to sail again." );

	DropAllLive ( pLand );
	m_emState = STATE_OVER;
	m_fStateTimer = OVER_TIME;
	CDebugSet::ToLogFile ( "[RIFT] run over at leg %d, %d players", m_nRound, (int) m_mapPlayers.size() );
}

void GLOdysseyRift::ResetRun ( GLLandMan* pLand )
{
	DropAllLive ( pLand );
	m_mapLeft.clear();
	m_emState = STATE_IDLE;
	m_nRound = 0;
	m_fStateTimer = 0;
}

// --------------------------------------------------------------------------- hooks

void GLOdysseyRift::OnCrowKilled ( GLCrow* pCrow )
{
	if ( !pCrow || !IsArenaLand ( pCrow->m_pLandMan ) ) return;

	int nTier = 0;
	for ( size_t i=0; i<m_vecLive.size(); ++i )
	{
		if ( m_vecLive[i].dwGlobID == pCrow->m_dwGlobID && m_vecLive[i].sID == pCrow->m_sNativeID )
		{
			nTier = m_vecLive[i].nTier;
			m_vecLive.erase ( m_vecLive.begin() + i );
			break;
		}
	}
	if ( nTier == 0 ) return;	// not a rift mob

	if ( nTier == 9 )
	{
		Announce ( "[NEXUS] A GOLDEN SOUL shatters! +1500 Essence to every voyager!" );
		for ( std::map<DWORD,SPLAYER>::iterator it = m_mapPlayers.begin(); it != m_mapPlayers.end(); ++it )
		{ it->second.dwEssence += 1500; it->second.dwEarned += 1500; }
	}

	const STARGETID& sKiller = pCrow->RiftAssault ();
	DWORD dwKillerGaea = GAEAID_NULL;
	if ( sKiller.emCrow == CROW_PC )	dwKillerGaea = sKiller.dwID;
	else if ( sKiller.emCrow == CROW_SUMMON )
	{
		PGLSUMMONFIELD pSummon = GLGaeaServer::GetInstance().GetSummon ( sKiller.dwID );
		if ( pSummon && pSummon->m_pOwner ) dwKillerGaea = pSummon->m_pOwner->m_dwGaeaID;	// credit the summoner
	}
	if ( dwKillerGaea == GAEAID_NULL ) return;
	std::map<DWORD,SPLAYER>::iterator it = m_mapPlayers.find ( dwKillerGaea );
	if ( it == m_mapPlayers.end() ) return;
	GLChar* pChar = GLGaeaServer::GetInstance().GetChar ( it->first );
	if ( !pChar || pChar->m_dwCharID != it->second.dwCharID ) return;

	const DWORD dwGain = KILL_ESSENCE * ( nTier == 3 ? 20 : ( nTier == 2 ? 2 : 1 ) );
	it->second.dwEssence += dwGain;
	it->second.dwEarned += dwGain;
	it->second.dwKills += 1;
	if ( nTier == 3 )
		Announce ( "[NEXUS] %s fells the Convergence! The rift screams.", pChar->m_szName );
}

bool GLOdysseyRift::CanUseSkill ( GLChar* pChar, const GLSKILL* pSkill )
{
	if ( !pChar || !pSkill || !IsArenaLand ( pChar->m_pLandMan ) ) return true;
	SPLAYER* pPlayer = FindPlayer ( pChar );
	if ( !pPlayer ) return true;

	const DWORD dwNeed = pSkill->m_sLEARN.sLVL_STEP[0].dwLEVEL;
	if ( dwNeed <= pPlayer->dwSkillTier ) return true;
	if ( pPlayer->fWarnCool <= 0 )
	{
		Tell ( pChar, "That skill (Lv%u) is sealed in the rift. Buy it back at a skill shrine.", dwNeed );
		pPlayer->fWarnCool = 3.0f;
	}
	return false;
}

GLOdysseyRift::SPLAYER* GLOdysseyRift::FindPlayer ( GLChar* pChar )
{
	std::map<DWORD,SPLAYER>::iterator it = m_mapPlayers.find ( pChar->m_dwGaeaID );
	if ( it == m_mapPlayers.end() || it->second.dwCharID != pChar->m_dwCharID ) return NULL;
	return &it->second;
}

// --------------------------------------------------------------------------- chat

BOOL GLOdysseyRift::OnChat ( GLChar* pChar, const char* szMsg )
{
	if ( !pChar || !szMsg ) return FALSE;

	if ( strncmp ( szMsg, "rift", 4 ) == 0 && ( szMsg[4] == ' ' || szMsg[4] == 0 ) )
		return CmdGM ( pChar, szMsg + 4 );
	if ( !m_bEnabled ) return FALSE;

	// Entry: climb into the Horse in Mystic Peak Square.
	if ( strcmp ( szMsg, "sail" ) == 0 )
	{
		const bool bGM = pChar->m_dwUserLvl >= USER_GM3;
		if ( pChar->m_sMapID != m_sEntryMap && !bGM ) return FALSE;
		if ( !bGM && Dist2D ( pChar->m_vPos, m_vEntryPos.x, m_vEntryPos.z ) > m_fEntryRadius )
		{
			Tell ( pChar, "The Horse is in the fountain of Mystic Peak Square. Stand by it." );
			return TRUE;
		}
		if ( !IsAlive ( pChar ) ) return TRUE;
		if ( pChar->m_sCONFTING.IsCONFRONTING() || pChar->m_sTrade.Valid() || pChar->m_sPMarket.IsOpen() )
		{
			Tell ( pChar, "Finish your duel, trade or shop before you board the Horse." );
			return TRUE;
		}
		Tell ( pChar, "The hatch creaks open. You climb into the dark belly of the Horse..." );
		if ( !Recall ( pChar, m_sArenaMap, m_vNexus ) ) Tell ( pChar, "The Horse will not move. (recall failed)" );
		return TRUE;
	}

	if ( !IsArenaLand ( pChar->m_pLandMan ) ) return FALSE;
	SPLAYER* pPlayer = FindPlayer ( pChar );
	if ( !pPlayer ) return FALSE;

	if ( strcmp ( szMsg, "essence" ) == 0 )	{ Tell ( pChar, "Essence: %u  Kills: %u  Leg: %d", pPlayer->dwEssence, pPlayer->dwKills, m_nRound ); return TRUE; }
	if ( strcmp ( szMsg, "shop" ) == 0 )	{ CmdShop ( pChar ); return TRUE; }
	if ( strcmp ( szMsg, "buy" ) == 0 )		{ CmdBuy ( pChar, pPlayer ); return TRUE; }
	if ( strcmp ( szMsg, "ready" ) == 0 )
	{
		Tell ( pChar, "Stand on the Nexus at the heart of the island. All voyagers must charge it." );
		return TRUE;
	}

	// Surprise: the name of home, spoken at the Nexus between legs, once per voyage.
	if ( _stricmp ( szMsg, "ithaca" ) == 0 && m_emState == STATE_CHARGING && m_nRound >= 1 && !m_bSecretIthaca
		&& Dist2D ( pChar->m_vPos, m_vNexus.x, m_vNexus.z ) <= m_fNexusRadius )
	{
		m_bSecretIthaca = true;
		Announce ( "[ATHENA] %s remembers the way home. I will light it for you.", pChar->m_szName );
		for ( std::map<DWORD,SPLAYER>::iterator it = m_mapPlayers.begin(); it != m_mapPlayers.end(); ++it )
		{
			it->second.dwEssence += 1000; it->second.dwEarned += 1000;
			GLChar* p = GLGaeaServer::GetInstance().GetChar ( it->first );
			if ( IsAlive ( p ) ) { p->m_sHP.TO_FULL(); p->m_sMP.TO_FULL(); p->m_sSP.TO_FULL(); p->MsgSendUpdateState (); }
		}
		Announce ( "[ATHENA] +1000 Essence and full strength to every voyager." );
		return FALSE;	// still shows as normal chat
	}
	return FALSE;
}

void GLOdysseyRift::CmdShop ( GLChar* pChar )
{
	Tell ( pChar, "-- Wares of the rift (stand by one and type 'buy') --" );
	for ( size_t i=0; i<m_vecShops.size(); ++i )
	{
		const SSHOP& s = m_vecShops[i];
		const bool bOpen = s.nZone < 0 || s.nZone >= (int) m_vecZones.size() || m_vecZones[s.nZone].bOpen;
		Tell ( pChar, "%s - %u Essence%s", s.strName.c_str(), s.dwPrice, bOpen ? "" : " (beyond a seal)" );
	}
	for ( size_t i=0; i<m_vecZones.size(); ++i )
		if ( !m_vecZones[i].bOpen ) Tell ( pChar, "Rift Seal %d - %u Essence", m_vecZones[i].nID, m_vecZones[i].dwPrice );
}

void GLOdysseyRift::CmdBuy ( GLChar* pChar, SPLAYER* pPlayer )
{
	if ( !IsAlive ( pChar ) ) return;

	// Nearest seal or ware within reach.
	int nSeal = -1, nShop = -1; float fBest = BUY_RANGE;
	for ( size_t i=0; i<m_vecZones.size(); ++i )
	{
		const SZONE& z = m_vecZones[i];
		if ( z.bOpen ) continue;
		const float d = Dist2D ( pChar->m_vPos, z.fSealX, z.fSealZ );
		if ( d < fBest ) { fBest = d; nSeal = (int) i; nShop = -1; }
	}
	for ( size_t i=0; i<m_vecShops.size(); ++i )
	{
		const float d = Dist2D ( pChar->m_vPos, m_vecShops[i].fX, m_vecShops[i].fZ );
		if ( d < fBest ) { fBest = d; nShop = (int) i; nSeal = -1; }
	}

	if ( nSeal < 0 && nShop < 0 ) { Tell ( pChar, "Nothing to buy here. Type 'shop' to see the wares." ); return; }

	if ( nSeal >= 0 )
	{
		SZONE& z = m_vecZones[nSeal];
		if ( pPlayer->dwEssence < z.dwPrice ) { Tell ( pChar, "The seal needs %u Essence. You have %u.", z.dwPrice, pPlayer->dwEssence ); return; }
		pPlayer->dwEssence -= z.dwPrice;
		z.bOpen = true;
		GLLandMan* pLand = ArenaLand ();
		SafeDropOut ( pLand, z.dwSealGlobID, m_sSealNpc );
		z.dwSealGlobID = UINT_MAX;
		Announce ( "[NEXUS] %s shatters Rift Seal %d! New ground - and new horrors - open.", pChar->m_szName, z.nID );
		return;
	}

	SSHOP& s = m_vecShops[nShop];
	if ( s.nZone >= 0 && s.nZone < (int) m_vecZones.size() && !m_vecZones[s.nZone].bOpen )
	{ Tell ( pChar, "That ware lies beyond a Rift Seal." ); return; }
	if ( pPlayer->dwEssence < s.dwPrice ) { Tell ( pChar, "%s costs %u Essence. You have %u.", s.strName.c_str(), s.dwPrice, pPlayer->dwEssence ); return; }

	if ( s.strKind == "heal" )
	{
		pChar->m_sHP.TO_FULL(); pChar->m_sMP.TO_FULL(); pChar->m_sSP.TO_FULL();
		pChar->MsgSendUpdateState ();
	}
	else if ( s.strKind == "skills" )
	{
		if ( pPlayer->dwSkillTier >= (DWORD) s.nParam ) { Tell ( pChar, "You already command those skills." ); return; }
		pPlayer->dwSkillTier = (DWORD) s.nParam;
	}
	else if ( s.strKind == "overclock" )
	{
		// Disabled: it would permanently upgrade a real, tradeable item from a free currency.
		// Needs a run-scoped upgrade (restore the grade on leaving) before it comes back.
		Tell ( pChar, "The Forge of Hephaestus is cold. It will burn again in a later voyage." );
		return;
	}
	else { Tell ( pChar, "The ware crumbles. (unknown kind %s)", s.strKind.c_str() ); return; }

	pPlayer->dwEssence -= s.dwPrice;
	Tell ( pChar, "Bought %s. Essence left: %u.", s.strName.c_str(), pPlayer->dwEssence );
}

BOOL GLOdysseyRift::CmdGM ( GLChar* pChar, const char* szArgs )
{
	if ( pChar->m_dwUserLvl < USER_GM3 ) return FALSE;
	while ( *szArgs == ' ' ) ++szArgs;

	char szCmd[32] = ""; int a = 0, b = 0;
	sscanf ( szArgs, "%31s %d %d", szCmd, &a, &b );
	std::string strCmd = szCmd;

	if ( strCmd == "pos" )
	{
		Tell ( pChar, "map %d %d  pos %.0f %.0f (y %.0f)", pChar->m_sMapID.wMainID, pChar->m_sMapID.wSubID,
			pChar->m_vPos.x, pChar->m_vPos.z, pChar->m_vPos.y );
	}
	else if ( strCmd == "reload" )
	{
		GLLandMan* pLand = ArenaLand ();
		if ( pLand ) ResetRun ( pLand );
		if ( m_dwEntryNpcGlobID != UINT_MAX )
		{
			GLLandMan* pEntry = GLGaeaServer::GetInstance().GetByMapID ( m_sEntryMap );
			SafeDropOut ( pEntry, m_dwEntryNpcGlobID, m_sEntryNpc );
			m_dwEntryNpcGlobID = UINT_MAX;
		}
		for ( size_t i=0; i<m_vecZones.size(); ++i )
		{
			SafeDropOut ( pLand, m_vecZones[i].dwSealGlobID, m_sSealNpc );
			m_vecZones[i].dwSealGlobID = UINT_MAX;
		}
		for ( size_t i=0; i<m_vecNpcs.size(); ++i )
			SafeDropOut ( pLand, m_vecNpcs[i].dwGlobID, m_vecNpcs[i].sID );
		m_bSetup = false;
		const bool bOK = LoadConfig ();
		Tell ( pChar, "rift config reloaded: %s, %d zones, %d wares", bOK ? "on" : "OFF",
			(int) m_vecZones.size(), (int) m_vecShops.size() );
	}
	else if ( strCmd == "round" && a > 0 )
	{
		m_nRound = ( a > 200 ? 200 : a ) - 1;
		if ( m_emState == STATE_WAVE ) DropAllLive ( ArenaLand () );
		if ( m_emState == STATE_WAVE || m_emState == STATE_CHARGING ) StartWave ();
		Tell ( pChar, "rift: jumped to leg %d", m_nRound );
	}
	else if ( strCmd == "essence" )
	{
		SPLAYER* pPlayer = FindPlayer ( pChar );
		if ( pPlayer ) { pPlayer->dwEssence += (DWORD) a; Tell ( pChar, "Essence: %u", pPlayer->dwEssence ); }
	}
	else if ( strCmd == "reset" )
	{
		GLLandMan* pLand = ArenaLand ();
		if ( pLand ) ResetRun ( pLand );
		Tell ( pChar, "rift: reset" );
	}
	else if ( strCmd == "mobs" )
	{
		std::map<DWORD,int> mapCount;
		GLLandMan* pLand = ArenaLand ();
		Tell ( pChar, "rift: %d mob types, %d spawn points, %d alive, state %d leg %d",
			(int) m_vecMobs.size(), (int) m_vecSpawns.size(), (int) m_vecLive.size(), (int) m_emState, m_nRound );
		for ( size_t i=0; i<m_vecMobs.size() && i<12; ++i )
			Tell ( pChar, "  mob %d %d tier %d", m_vecMobs[i].sID.wMainID, m_vecMobs[i].sID.wSubID, m_vecMobs[i].nTier );
		(void) pLand; (void) mapCount;
	}
	else if ( strCmd == "spawn" )
	{
		GLLandMan* pLand = pChar->m_pLandMan;
		DWORD dwGlob = pLand ? pLand->DropCrow ( SNATIVEID ( (WORD)a, (WORD)b ), pChar->m_vPos.x + 60, pChar->m_vPos.z ) : UINT_MAX;
		Tell ( pChar, "rift: spawn %d %d -> %u", a, b, dwGlob );
	}
	else
	{
		Tell ( pChar, "rift pos | reload | round N | essence N | reset | mobs | spawn M S" );
	}
	return TRUE;
}
