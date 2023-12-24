//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#include "cbase.h"
#include "c_prop_portal.h"
#include "portal_shareddefs.h"
#include "clientsideeffects.h"
#include "tier0/vprof.h"
#include "materialsystem/itexture.h"
#include "hud_macros.h"
#include "igamesystem.h"
#include "view.h"						// For MainViewOrigin()
#include "clientleafsystem.h"			// For finding the leaves our portals are in
#include "portal_render_targets.h"		// Access to static references to Portal-specific render textures
#include "toolframework/itoolframework.h"
#include "toolframework_client.h"
#include "tier1/KeyValues.h"
#include "rendertexture.h"
#include "prop_portal_shared.h"
#include "particles_new.h"

#include "c_portal_player.h"

#include "c_pixel_visibility.h"

#include "glow_overlay.h"

#include "dlight.h"
#include "iefx.h"

#include "simple_keys.h"

#ifdef _DEBUG
#include "filesystem.h"
#endif

#include "debugoverlay_shared.h"


LINK_ENTITY_TO_CLASS( prop_portal, C_Prop_Portal );

IMPLEMENT_CLIENTCLASS_DT( C_Prop_Portal, DT_Prop_Portal, CProp_Portal )
	RecvPropEHandle( RECVINFO(m_hLinkedPortal) ),
	RecvPropBool( RECVINFO(m_bActivated) ),
	RecvPropBool( RECVINFO(m_bIsPortal2) ),
	RecvPropVector( RECVINFO(m_vPortalVelocity) ),
	RecvPropEHandle( RECVINFO(m_hRelativeEntity) ),
END_RECV_TABLE()


void __MsgFunc_EntityPortalled( bf_read &msg )
{
	long iEncodedEHandle;
	int iEntity, iSerialNum;


	//grab portal EHANDLE
	iEncodedEHandle = msg.ReadLong();

	if( iEncodedEHandle == INVALID_NETWORKED_EHANDLE_VALUE )
		return;

	iEntity = iEncodedEHandle & ((1 << MAX_EDICT_BITS) - 1);
	iSerialNum = iEncodedEHandle >> MAX_EDICT_BITS;

	EHANDLE hPortal( iEntity, iSerialNum );
	C_Prop_Portal *pPortal = ( C_Prop_Portal *)(hPortal.Get());

	if( pPortal == NULL )
		return;



	//grab other entity's EHANDLE

	iEncodedEHandle = msg.ReadLong();

	if( iEncodedEHandle == INVALID_NETWORKED_EHANDLE_VALUE )
		return;	

	iEntity = iEncodedEHandle & ((1 << MAX_EDICT_BITS) - 1);
	iSerialNum = iEncodedEHandle >> MAX_EDICT_BITS;

	EHANDLE hEntity( iEntity, iSerialNum );
	C_BaseEntity *pEntity = hEntity.Get();
	if( pEntity == NULL )
		return;



	bool bIsPlayer = pEntity->IsPlayer();

	Vector ptNewPosition;
	ptNewPosition.x = msg.ReadFloat();
	ptNewPosition.y = msg.ReadFloat();
	ptNewPosition.z = msg.ReadFloat();
	QAngle qNewAngles;
	qNewAngles.x = msg.ReadFloat();
	qNewAngles.y = msg.ReadFloat();
	qNewAngles.z = msg.ReadFloat();

	Vector vecOldInterpolatedPos;
	QAngle qaOldInterpolatedRot;
	if ( pEntity->IsToolRecording() )
	{
		vecOldInterpolatedPos = pEntity->GetOriginInterpolator().GetCurrent();
		qaOldInterpolatedRot = pEntity->GetRotationInterpolator().GetCurrent();
	}

	pEntity->AddEFlags( EFL_DIRTY_ABSTRANSFORM );

	VMatrix matTransform = pPortal->MatrixThisToLinked();
	//VMatrix matInvTransform = pPortal->m_hLinkedPortal->MatrixThisToLinked();

	CInterpolatedVar< QAngle > &rotInterp = pEntity->GetRotationInterpolator();
	CInterpolatedVar< Vector > &posInterp = pEntity->GetOriginInterpolator();

	Vector ptCurrentPosition = posInterp.GetCurrent();
	Vector ptInvCurrentPosition = matTransform * ptCurrentPosition;
	bool bAlreadyTeleported = ((ptCurrentPosition - ptNewPosition).LengthSqr() < (ptInvCurrentPosition - ptNewPosition).LengthSqr()); //newest network update closer to destination than transformed to destination indicates it's already been transformed


	UTIL_TransformInterpolatedAngle( rotInterp, matTransform.As3x4(), bAlreadyTeleported );

	if( bIsPlayer ) //the player's origin != player's center, transforms are performed about centers
	{
		VMatrix matShiftOrigin;
		matShiftOrigin.Identity();
		Vector vTranslate( 0.0f, 0.0f, 36.0f );
		matShiftOrigin.SetTranslation( vTranslate );
		matTransform = matTransform * matShiftOrigin;
		vTranslate = -vTranslate;
		matShiftOrigin.SetTranslation( vTranslate );
		matTransform = matShiftOrigin * matTransform;
	}

	UTIL_TransformInterpolatedPosition( posInterp, matTransform, bAlreadyTeleported );


	if( bIsPlayer )
		((C_Portal_Player *)pEntity)->PlayerPortalled( pPortal );

	if ( pEntity->IsToolRecording() )
	{
		static EntityTeleportedRecordingState_t state;

		KeyValues *msg = new KeyValues( "entity_teleported" );
		msg->SetPtr( "state", &state );
		state.m_bTeleported = true;
		state.m_bViewOverride = false;
		state.m_vecTo = ptNewPosition;
		state.m_qaTo = qNewAngles;
		state.m_teleportMatrix = matTransform.As3x4();

		// Post a message back to all IToolSystems
		Assert( (int)pEntity->GetToolHandle() != 0 );
		ToolFramework_PostToolMessage( pEntity->GetToolHandle(), msg );

		msg->deleteThis();
	}
}

static ConVar portal_demohack( "portal_demohack", "0", FCVAR_ARCHIVE, "Do the demo_legacy_rollback setting to help during demo playback of going through portals." );

class C_PortalInitHelper : public CAutoGameSystem
{
	virtual bool Init()
	{
		//HOOK_MESSAGE( PlayerPortalled );
		HOOK_MESSAGE( EntityPortalled );
		if ( portal_demohack.GetBool() )
		{
			ConVarRef demo_legacy_rollback_ref( "demo_legacy_rollback" );
			demo_legacy_rollback_ref.SetValue( false ); //Portal demos are wrong if the eyes rollback as far as regular demos
		}
		// However, there are probably bugs with this when jump ducking, etc.
		return true;
	}
};
static C_PortalInitHelper s_PortalInitHelper;



C_Prop_Portal::C_Prop_Portal( void )
{
	TransformedLighting.m_LightShadowHandle = CLIENTSHADOW_INVALID_HANDLE;
	CProp_Portal_Shared::AllPortals.AddToTail( this );
}

C_Prop_Portal::~C_Prop_Portal( void )
{
	CProp_Portal_Shared::AllPortals.FindAndRemove( this );
	g_pPortalRender->RemovePortal( this );

	for( int i = m_GhostRenderables.Count(); --i >= 0; )
	{
		delete m_GhostRenderables[i];
	}
	m_GhostRenderables.RemoveAll();
}

void C_Prop_Portal::Spawn( void )
{
	SetParent( NULL );
	SetThink( &C_Prop_Portal::ClientThink );
	SetNextClientThink( CLIENT_THINK_ALWAYS );

	m_matrixThisToLinked.Identity(); //don't accidentally teleport objects to zero space
	BaseClass::Spawn();
}

void C_Prop_Portal::Activate( void )
{
	BaseClass::Activate();
}

void C_Prop_Portal::ClientThink( void )
{
	//reseting the model once a server frame isn't enough as server is 65fps and client goes much higher
	//so if the portal is moving, call data changed
	if ( m_vPortalVelocity != vec3_origin )
		OnDataChanged( DATA_UPDATE_DATATABLE_CHANGED );

	if( m_fStaticAmount > 0.0f )
	{
		m_fStaticAmount -= gpGlobals->absoluteframetime;
		if( m_fStaticAmount < 0.0f ) 
			m_fStaticAmount = 0.0f;
	}
	if( m_fSecondaryStaticAmount > 0.0f )
	{
		m_fSecondaryStaticAmount -= gpGlobals->absoluteframetime;
		if( m_fSecondaryStaticAmount < 0.0f ) 
			m_fSecondaryStaticAmount = 0.0f;
	}

	if( m_fOpenAmount < 1.0f )
	{
		m_fOpenAmount += gpGlobals->absoluteframetime * 2.0f;
		if ( m_fOpenAmount > 1.0f )
		{
			m_fOpenAmount = 1.0f;
		}
	}
}

void C_Prop_Portal::Simulate()
{
	BaseClass::Simulate();

	//clear list of ghosted entities from last frame, and clear the clipping planes we put on them
	for( int i = m_hGhostingEntities.Count(); --i >= 0; )
	{
		C_BaseEntity *pEntity = m_hGhostingEntities[i].Get();

		if( pEntity != NULL )
			pEntity->m_bEnableRenderingClipPlane = false;
	}
	m_hGhostingEntities.RemoveAll();


	if( !IsActivedAndLinked() )
	{
		//remove all ghost renderables
		for( int i = m_GhostRenderables.Count(); --i >= 0; )
		{
			delete m_GhostRenderables[i];
		}
		
		m_GhostRenderables.RemoveAll();

		return;
	}



	//Find objects that are intersecting the portal and mark them for later replication on the remote portal's side
	C_Portal_Player *pLocalPlayer = C_Portal_Player::GetLocalPlayer();
	C_BaseViewModel *pLocalPlayerViewModel = pLocalPlayer->GetViewModel();

	CBaseEntity *pEntsNearPortal[1024];
	int iEntsNearPortal = UTIL_EntitiesInSphere( pEntsNearPortal, 1024, GetAbsOrigin(), PORTAL_HALF_HEIGHT, 0, PARTITION_CLIENT_NON_STATIC_EDICTS );

	if( iEntsNearPortal != 0 )
	{
		float fClipPlane[4];
		fClipPlane[0] = m_plane_Origin.normal.x;
		fClipPlane[1] = m_plane_Origin.normal.y;
		fClipPlane[2] = m_plane_Origin.normal.z;
		fClipPlane[3] = m_plane_Origin.dist - 0.3f;

		for( int i = 0; i != iEntsNearPortal; ++i )
		{
			CBaseEntity *pEntity = pEntsNearPortal[i];
			Assert( pEntity != NULL );

			bool bIsMovable = false;

			C_BaseEntity *pMoveEntity = pEntity;

			if ( FClassnameIs( pMoveEntity, "C_Prop_Portal" ) )
				continue;

			MoveType_t moveType = MOVETYPE_NONE;

			//unmoveables and doors can never get halfway in the portal
			while ( pMoveEntity )
			{
				moveType = pMoveEntity->GetMoveType();

				if ( !( moveType == MOVETYPE_NONE || moveType == MOVETYPE_PUSH ) )
				{
					bIsMovable = true;
					pMoveEntity = NULL;
				}
				else
					pMoveEntity = pMoveEntity->GetMoveParent();
			}

			if ( !bIsMovable )
				continue;

			Assert( dynamic_cast<C_Prop_Portal *>(pMoveEntity) == NULL ); //should have been killed with (pEntity->GetMoveType() == MOVETYPE_NONE) check. Infinite recursion is infinitely bad.

			if( pEntity == pLocalPlayerViewModel )
				continue; //avoid ghosting view models

			bool bActivePlayerWeapon = false;

			C_BaseCombatWeapon *pWeapon = dynamic_cast<C_BaseCombatWeapon*>( pEntity );
			if ( pWeapon )
			{
				C_Portal_Player *pPortalPlayer = ToPortalPlayer( pWeapon->GetOwner() );
				if ( pPortalPlayer ) 
				{
					if ( pPortalPlayer->GetActiveWeapon() != pWeapon )
						continue; // don't ghost player owned non selected weapons
					else
						bActivePlayerWeapon = true;
				}
			}

			Vector ptEntCenter = pEntity->WorldSpaceCenter();
			if( bActivePlayerWeapon )
				ptEntCenter = pWeapon->GetOwner()->WorldSpaceCenter();

			if( (m_plane_Origin.normal.Dot( ptEntCenter ) - m_plane_Origin.dist) < -5.0f )
				continue; //entity is behind the portal, most likely behind the wall the portal is placed on

			if( !CProp_Portal_Shared::IsEntityTeleportable( pEntity ) )
				continue;

			if ( bActivePlayerWeapon )
			{
				if( !m_PortalSimulator.EntityHitBoxExtentIsInPortalHole( pWeapon->GetOwner() ) && 
					!m_PortalSimulator.EntityHitBoxExtentIsInPortalHole( pWeapon ) )  // TODO: add some code here to prevent clipping when not in the visual portal
					continue;
			}
			else if( pEntity->IsPlayer() )
			{
				if( !m_PortalSimulator.EntityHitBoxExtentIsInPortalHole( (C_BaseAnimating*)pEntity ) )
					continue;
			}
			else
			{
				if( !m_PortalSimulator.EntityIsInPortalHole( pEntity ) )
					continue;
			}

			pEntity->m_bEnableRenderingClipPlane = true;
			memcpy( pEntity->m_fRenderingClipPlane, fClipPlane, sizeof( float ) * 4 );

			EHANDLE hEnt = pEntity;
			m_hGhostingEntities.AddToTail( hEnt );
		}
	}

	//now, fix up our list of ghosted renderables.
	{
		bool *bStillInUse = (bool *)stackalloc( sizeof( bool ) * (m_GhostRenderables.Count() + m_hGhostingEntities.Count()) );
		memset( bStillInUse, 0, sizeof( bool ) * (m_GhostRenderables.Count() + m_hGhostingEntities.Count()) );

		for( int i = m_hGhostingEntities.Count(); --i >= 0; )
		{
			C_BaseEntity *pRenderable = m_hGhostingEntities[i].Get();

			int j;
			for( j = m_GhostRenderables.Count(); --j >= 0; )
			{
				if( pRenderable == m_GhostRenderables[j]->m_pGhostedRenderable )
				{
					bStillInUse[j] = true;
					m_GhostRenderables[j]->PerFrameUpdate();
					break;
				}
			}
			
			if ( j >= 0 )
				continue;

			//newly added
			C_BaseEntity *pEntity = m_hGhostingEntities[i];

			bool bIsHeldWeapon = false;
			C_BaseCombatWeapon *pWeapon = dynamic_cast<C_BaseCombatWeapon*>( pEntity );
			if ( pWeapon && ToPortalPlayer( pWeapon->GetOwner() ) )
				bIsHeldWeapon = true;

			C_PortalGhostRenderable *pNewGhost = new C_PortalGhostRenderable( this,
																				pRenderable, 
																				pEntity->GetRenderGroup(), 
																				m_matrixThisToLinked, 
																				m_fGhostRenderablesClip,
																				(pEntity == pLocalPlayer || bIsHeldWeapon) );
			Assert( pNewGhost );

			bStillInUse[ m_GhostRenderables.AddToTail( pNewGhost ) ] = true;
			pNewGhost->PerFrameUpdate();

			// HACK - I just copied the CClientTools::OnEntityCreated code here,
			// since the ghosts aren't really entities - they don't have an entindex,
			// they're not in the entitylist, and they get created during Simulate(),
			// which isn't valid for real entities, since it changes the simulate list
			// -jd
			if ( ToolsEnabled() && clienttools->IsInRecordingMode() )
			{
				// Send deletion message to tool interface
				KeyValues *kv = new KeyValues( "created" );
				HTOOLHANDLE h = clienttools->AttachToEntity( pNewGhost );
				ToolFramework_PostToolMessage( h, kv );

				kv->deleteThis();
			}
		}

		//remove unused ghosts
		for ( int i = m_GhostRenderables.Count(); --i >= 0; )
		{
			if ( bStillInUse[i] )
				continue;

			// HACK - I just copied the CClientTools::OnEntityDeleted code here,
			// since the ghosts aren't really entities - they don't have an entindex,
			// they're not in the entitylist, and they get created during Simulate(),
			// which isn't valid for real entities, since it changes the simulate list
			// -jd
			C_PortalGhostRenderable *pGhost = m_GhostRenderables[i];
			if ( ToolsEnabled() )
			{
				HTOOLHANDLE handle = pGhost ? pGhost->GetToolHandle() : (HTOOLHANDLE)0;
				if ( handle != (HTOOLHANDLE)0 )
				{
					if ( clienttools->IsInRecordingMode() )
					{
						// Send deletion message to tool interface
						KeyValues *kv = new KeyValues( "deleted" );
						ToolFramework_PostToolMessage( handle, kv );
						kv->deleteThis();
					}

					clienttools->DetachFromEntity( pGhost );
				}
			}

			delete pGhost;
			m_GhostRenderables.FastRemove( i );
		}
	}

	//ensure the shared clip plane is up to date
	C_Prop_Portal *pLinkedPortal = m_hLinkedPortal.Get();

	m_fGhostRenderablesClip[0] = pLinkedPortal->m_plane_Origin.normal.x;
	m_fGhostRenderablesClip[1] = pLinkedPortal->m_plane_Origin.normal.y;
	m_fGhostRenderablesClip[2] = pLinkedPortal->m_plane_Origin.normal.z;
	m_fGhostRenderablesClip[3] = pLinkedPortal->m_plane_Origin.dist - 0.75f;
}

void C_Prop_Portal::UpdateOnRemove( void )
{
	if( TransformedLighting.m_LightShadowHandle != CLIENTSHADOW_INVALID_HANDLE )
	{
		g_pClientShadowMgr->DestroyFlashlight( TransformedLighting.m_LightShadowHandle );
		TransformedLighting.m_LightShadowHandle = CLIENTSHADOW_INVALID_HANDLE;
	}

	g_pPortalRender->RemovePortal( this );

	BaseClass::UpdateOnRemove();
}

void C_Prop_Portal::OnNewParticleEffect( const char *pszParticleName, CNewParticleEffect *pNewParticleEffect )
{
	if ( Q_stricmp( pszParticleName, "portal_1_overlap" ) == 0 || Q_stricmp( pszParticleName, "portal_2_overlap" ) == 0 )
	{
		float fClosestDistanceSqr = -1.0f;
		Vector vClosestPosition;

		int iPortalCount = CProp_Portal_Shared::AllPortals.Count();
		if( iPortalCount != 0 )
		{
			CProp_Portal **pPortals = CProp_Portal_Shared::AllPortals.Base();
			for( int i = 0; i != iPortalCount; ++i )
			{
				CProp_Portal *pTempPortal = pPortals[i];
				if ( pTempPortal != this && pTempPortal->m_bActivated )
				{
					Vector vPosition = pTempPortal->GetAbsOrigin();

					float fDistanceSqr = pNewParticleEffect->GetRenderOrigin().DistToSqr( vPosition );

					if ( fClosestDistanceSqr == -1.0f || fClosestDistanceSqr > fDistanceSqr )
					{
						fClosestDistanceSqr = fDistanceSqr;
						vClosestPosition = vPosition;
					}
				}
			}
		}

		if ( fClosestDistanceSqr != -1.0f )
		{
			pNewParticleEffect->SetControlPoint( 1, vClosestPosition );
		}
	}
}

void C_Prop_Portal::OnPreDataChanged( DataUpdateType_t updateType )
{
	PreDataChanged.m_matrixThisToLinked = m_matrixThisToLinked;
	PreDataChanged.m_bIsPortal2 = m_bIsPortal2;
	PreDataChanged.m_bActivated = m_bActivated;
	PreDataChanged.m_vOrigin = GetAbsOrigin();
	PreDataChanged.m_qAngles = GetAbsAngles();
	PreDataChanged.m_hLinkedTo = m_hLinkedPortal.Get();

	BaseClass::OnPreDataChanged( updateType );
}

//ConVar r_portal_light_innerangle( "r_portal_light_innerangle", "90.0", FCVAR_CLIENTDLL );
//ConVar r_portal_light_outerangle( "r_portal_light_outerangle", "90.0", FCVAR_CLIENTDLL );
//ConVar r_portal_light_forward( "r_portal_light_forward", "0.0", FCVAR_CLIENTDLL );


void C_Prop_Portal::OnDataChanged( DataUpdateType_t updateType )
{
	ResetModel();
#if 0
	DevMsg( "Portal: %i. Velocity: %.2f x, %.2f y, %.2f z \n", m_bIsPortal2?2:1, m_vPortalVelocity.x, m_vPortalVelocity.y, m_vPortalVelocity.z );
#endif

	C_Prop_Portal *pRemote = m_hLinkedPortal;
	m_pLinkedPortal = pRemote;
	GetVectors( &m_vForward, &m_vRight, &m_vUp );
	m_ptOrigin = GetAbsOrigin();

	bool bPortalMoved = ( 
						( PreDataChanged.m_vOrigin != m_ptOrigin ) ||
						( PreDataChanged.m_qAngles != GetAbsAngles() ) || 
						( PreDataChanged.m_bActivated == false ) ||
						( PreDataChanged.m_bIsPortal2 != m_bIsPortal2 ) 
						);

	//CBasePlayer *pPlayer = CBasePlayer::GetLocalPlayer();
	// I tried having weapon_portalgun influence if the opening animation should play, but the network variable doesn't update in time for datachanged, so i'll use the static variable trick
	bool bNewPortal = false;
	bool bPortalMovedFar = ( ( PreDataChanged.m_vOrigin - m_ptOrigin ).Length() > 50 );
	static bool iNewPortal1 = false;
	static bool iNewPortal2 = false;
	if ( bPortalMovedFar )
	{
		if ( m_bIsPortal2 )
			iNewPortal2 = false;
		else
			iNewPortal1 = false;
	}
	if ( !iNewPortal1 && !m_bIsPortal2 )
	{
		iNewPortal1 = true;
		bNewPortal = true;
	}
	if ( !iNewPortal2 && m_bIsPortal2 )
	{
		iNewPortal2 = true;
		bNewPortal = true;
	}

	bool bNewLinkage = ( (PreDataChanged.m_hLinkedTo.Get() != m_hLinkedPortal.Get()) );
	if( bNewLinkage )
		m_PortalSimulator.DetachFromLinked(); //detach now so moves are theoretically faster

	if( m_bActivated )
	{
		//generic stuff we'll need
		Vector vRemoteUp, vRemoteRight, vRemoteForward, ptRemoteOrigin;
		if( pRemote )
		{
			pRemote->GetVectors( &vRemoteForward, &vRemoteRight, &vRemoteUp );
			ptRemoteOrigin = pRemote->GetAbsOrigin();
		}
		g_pPortalRender->AddPortal( this ); //will know if we're already added and avoid adding twice
		 
		if( bPortalMoved )
		{			
			Vector ptForwardOrigin = m_ptOrigin + m_vForward;// * 3.0f;
			Vector vScaledRight = m_vRight * (PORTAL_HALF_WIDTH * 0.95f);
			Vector vScaledUp = m_vUp * (PORTAL_HALF_HEIGHT  * 0.95f);

			m_PortalSimulator.MoveTo( GetAbsOrigin(), GetAbsAngles() );

			//update our associated portal environment
			//CPortal_PhysicsEnvironmentMgr::CreateEnvironment( this );
			
			if ( bNewPortal )
				m_fOpenAmount = 0.0f;

			SetNextClientThink( CLIENT_THINK_ALWAYS ); //we need this to help open up

			//add static to the remote
			if( pRemote )
			{
				if ( bNewPortal )
					pRemote->m_fStaticAmount = 1.0f; // This will cause the other portal to show the static effect
				pRemote->SetNextClientThink( CLIENT_THINK_ALWAYS );
			}
		}
	}
	else
	{
		g_pPortalRender->RemovePortal( this );

		m_PortalSimulator.DetachFromLinked();

		if( TransformedLighting.m_pEntityLight )
		{
			TransformedLighting.m_pEntityLight->die = gpGlobals->curtime;
			TransformedLighting.m_pEntityLight = NULL;
		}

		if( TransformedLighting.m_LightShadowHandle != CLIENTSHADOW_INVALID_HANDLE )
		{
			g_pClientShadowMgr->DestroyFlashlight( TransformedLighting.m_LightShadowHandle );
			TransformedLighting.m_LightShadowHandle = CLIENTSHADOW_INVALID_HANDLE;
		}
	}

	if( (PreDataChanged.m_hLinkedTo.Get() != m_hLinkedPortal.Get()) && m_hLinkedPortal.Get() )
		m_PortalSimulator.AttachTo( &m_hLinkedPortal.Get()->m_PortalSimulator );
	

	BaseClass::OnDataChanged( updateType );
	
	if( bNewLinkage || bPortalMoved )
	{
		PortalMoved(); //updates link matrix and internals
	}

	if ( bPortalMoved )
	{
		UpdateOriginPlane();
	}

	if( bPortalMoved || bNewLinkage )
	{
		UpdateGhostRenderables();
		if( pRemote )
			pRemote->UpdateGhostRenderables();
	}
}

void C_Prop_Portal::UpdateGhostRenderables( void )
{
	//lastly, update all ghost renderables
	for( int i = m_GhostRenderables.Count(); --i >= 0; )
	{
		m_GhostRenderables[i]->m_matGhostTransform = m_matrixThisToLinked;;
	}
}

extern ConVar building_cubemaps;

int C_Prop_Portal::DrawModel( int flags )
{
	// Don't draw in cube maps because it makes an ugly colored splotch
	if( m_bActivated == false || building_cubemaps.GetBool() )
		return 0;

	int iRetVal = 0;

	if ( !g_pPortalRender->ShouldUseStencilsToRenderPortals() )
	{
		DrawPortal();
	}

	if( WillUseDepthDoublerThisDraw() )
		m_fSecondaryStaticAmount = 0.0f;

	iRetVal = BaseClass::DrawModel( flags );

	return iRetVal;
}


//-----------------------------------------------------------------------------
// Handle recording for the SFM
//-----------------------------------------------------------------------------
void C_Prop_Portal::GetToolRecordingState( KeyValues *msg )
{
	if ( !ToolsEnabled() )
		return;

	VPROF_BUDGET( "C_Prop_Portal::GetToolRecordingState", VPROF_BUDGETGROUP_TOOLS );
	BaseClass::GetToolRecordingState( m_bActivated, msg );

	if ( !m_bActivated )
	{
		BaseEntityRecordingState_t *pBaseEntity = (BaseEntityRecordingState_t*)msg->GetPtr( "baseentity" );
		pBaseEntity->m_bVisible = false;
	}
}

void C_Prop_Portal::UpdateOriginPlane( void )
{
	//setup our origin plane
	GetVectors( &m_plane_Origin.normal, NULL, NULL );
	m_plane_Origin.dist = m_plane_Origin.normal.Dot( GetAbsOrigin() );
	m_plane_Origin.signbits = SignbitsForPlane( &m_plane_Origin );

	Vector vAbsNormal;
	vAbsNormal.x = fabs(m_plane_Origin.normal.x);
	vAbsNormal.y = fabs(m_plane_Origin.normal.y);
	vAbsNormal.z = fabs(m_plane_Origin.normal.z);

	if( vAbsNormal.x > vAbsNormal.y )
	{
		if( vAbsNormal.x > vAbsNormal.z )
		{
			if( vAbsNormal.x > 0.999f )
				m_plane_Origin.type = PLANE_X;
			else
				m_plane_Origin.type = PLANE_ANYX;
		}
		else
		{
			if( vAbsNormal.z > 0.999f )
				m_plane_Origin.type = PLANE_Z;
			else
				m_plane_Origin.type = PLANE_ANYZ;
		}
	}
	else
	{
		if( vAbsNormal.y > vAbsNormal.z )
		{
			if( vAbsNormal.y > 0.999f )
				m_plane_Origin.type = PLANE_Y;
			else
				m_plane_Origin.type = PLANE_ANYY;
		}
		else
		{
			if( vAbsNormal.z > 0.999f )
				m_plane_Origin.type = PLANE_Z;
			else
				m_plane_Origin.type = PLANE_ANYZ;
		}
	}
}

void C_Prop_Portal::SetIsPortal2( bool bValue )
{
	m_bIsPortal2 = bValue;
}

bool C_Prop_Portal::IsActivedAndLinked( void ) const
{
	return ( m_bActivated && m_hLinkedPortal.Get() != NULL );
}

void C_Prop_Portal::ResetModel( void )
{
	if ( !m_bIsPortal2 )
		SetModel( "models/portals/portal1.mdl" );
	else
		SetModel( "models/portals/portal2.mdl" );

	OnNewModel();

	SetSize( CProp_Portal_Shared::vLocalMins, CProp_Portal_Shared::vLocalMaxs );

	SetSolid( SOLID_OBB );
	SetSolidFlags( FSOLID_TRIGGER | FSOLID_NOT_SOLID | FSOLID_CUSTOMBOXTEST | FSOLID_CUSTOMRAYTEST );
}