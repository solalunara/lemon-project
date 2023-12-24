#include "cbase.h"
#include "FlyingPortal.h"
#include "physicsshadowclone.h"
#include "prop_portal.h"
#include "triggers.h"
#include "collisionutils.h"
#include "portal_placement.h"
#include "prop_portal.h"
#include "convar.h"
#include "vcollide.h"
#include "vcollide_parse.h"
#include "physics_saverestore.h"
#include "movevars_shared.h"

#define FRAMES_PER_THINK 1
#define MAX_MOVEDIST 99999

BEGIN_DATADESC( CFlyingPortal )
	DEFINE_FIELD( iLinkageGroup, FIELD_INTEGER ),
	DEFINE_FIELD( pPhysObject, FIELD_CLASSPTR ),
	DEFINE_THINKFUNC( FlyThink ),
	DEFINE_USEFUNC( Use )
END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( CFlyingPortal, DT_FlyingPortal )
	SendPropFloat( SENDINFO( fMoveDist ) ),
	SendPropInt( SENDINFO( iLinkageGroup ) ),
	SendPropBool( SENDINFO( bPortal2 ) ),
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( flying_portal, CFlyingPortal );

#define FLYING_PORTAL_MINS Vector( -.2, -.2, -.2 )
#define FLYING_PORTAL_MAXS Vector( .2, .2, .2 )


CFlyingPortal::CFlyingPortal()
{
	bHitObject = false;
	RegisterThinkContext( "FlyThinkContext" );
}

CFlyingPortal *CFlyingPortal::Create( Vector vStartPos, Vector vDirection, float fSpeed, int iLinkageGroup, bool bPortal2 )
{
	QAngle Angles;
	VectorAngles( vDirection, Angles );
	if ( vDirection.z < 0 )
	{
		vStartPos.z += 40 * vDirection.Normalized().z; //move past the player's feet
	}
	CFlyingPortal *pPortal = static_cast<CFlyingPortal *>( BaseClass::Create( "flying_portal", vStartPos, Angles ) );
	pPortal->pPhysObject->SetPosition( vStartPos, Angles, true );
	pPortal->qAngles = Angles;
	pPortal->vVelocity = vDirection.Normalized() * fSpeed;
	pPortal->pPhysObject->SetVelocity( &pPortal->vVelocity, NULL );
	pPortal->iLinkageGroup = iLinkageGroup;
	pPortal->bPortal2 = bPortal2;
	pPortal->SetContextThink( &CFlyingPortal::FlyThink, gpGlobals->curtime, "FlyThinkContext" );
	pPortal->SetCollisionGroup( COLLISION_GROUP_INTERACTIVE );


	byte r = bPortal2 ? 255 : 10;
	byte g = bPortal2 ? 140 : 0;
	byte b = bPortal2 ? 10 : 255;
	pPortal->SetRenderColor( r, g, b );

	pPortal->SetRenderMode( kRenderWorldGlow );

	return pPortal;
}


CFlyingPortal::~CFlyingPortal( void )
{
	
}

void CFlyingPortal::UpdateOnRemove( void )
{
	BaseClass::UpdateOnRemove();
}

void CFlyingPortal::Precache( void )
{
	BaseClass::Precache();
	PrecacheModel( "models/props/sphere.mdl" );
}


void CFlyingPortal::StopLoopingSounds()
{
	BaseClass::StopLoopingSounds();
}

void CFlyingPortal::Spawn( void )
{
	Precache();

	pPhysEnvironment = physenv;
	/*
	pPhysEnvironment = physics->CreateEnvironment();
	pPhysEnvironment->SetGravity( Vector( 0, 0, -1 ) );
	pPhysEnvironment->SetSimulationTimestep( DEFAULT_TICK_INTERVAL );
	pPhysEnvironment->SetCollisionEventHandler( &g_collisions );
	pPhysEnvironment->SetCollisionSolver( &g_collisions );
	*/

	SetModel( "models/props/sphere.mdl" );
	SetModelScale( 0.5f );
	SetSolid( SOLID_VPHYSICS );
	SetMoveType( MOVETYPE_VPHYSICS );
	SetSize( FLYING_PORTAL_MINS, FLYING_PORTAL_MAXS );



	solid_t tmpSolid;
	solid_t *pSolid = &tmpSolid;
	PhysModelParseSolidByIndex( tmpSolid, this, GetModelIndex(), -1 );

/*
	int surfaceProp = -1;
	if ( pSolid->surfaceprop[0] )
	{
		surfaceProp = physprops->GetSurfaceIndex( pSolid->surfaceprop );
	}
	IPhysicsObject *pObject = pPhysEnvironment->CreatePolyObject( pCollide->solids[pSolid->index], surfaceProp, vec3_origin, vec3_angle, &pSolid->params );

	if ( pObject )
	{
		if ( modelinfo->GetModelType( modelinfo->GetModel( GetModelIndex() ) ) == 1 )
		{
			unsigned int contents = modelinfo->GetModelContents( GetModelIndex() );
			Assert( contents != 0 );
			// HACKHACK: contents is used to filter collisions
			// HACKHACK: So keep solid on for water brushes since they should pass collision rules (as triggers)
			if ( contents & MASK_WATER )
			{
				contents |= CONTENTS_SOLID;
			}
			if ( contents != pObject->GetContents() && contents != 0 )
			{
				pObject->SetContents( contents );
				pObject->RecheckCollisionFilter();
			}
		}

		g_pPhysSaveRestoreManager->AssociateModel( pObject, GetModelIndex() );
	}

	pPhysObject = pObject;
	*/

	pPhysObject = VPhysicsInitNormal( SOLID_VPHYSICS, 0, false, pSolid );
	pPhysObject->EnableGravity( false );
	pPhysObject->EnableCollisions( true );
	pPhysObject->SetMass( 0.1f );
	pPhysObject->EnableDrag( false );
	pPhysObject->SetGameFlags( FVPHYSICS_WAS_THROWN );
	VPhysicsSetObject( pPhysObject );


	SetUse( &CFlyingPortal::Use );

	BaseClass::Spawn();
}

void CFlyingPortal::Activate( void )
{
	BaseClass::Activate();
}


void CFlyingPortal::OnRestore( void )
{
	BaseClass::OnRestore();
}


void CFlyingPortal::FlyThink( void )
{
	//PhysicsGameSystem();
	
	SetContextThink( &CFlyingPortal::FlyThink, gpGlobals->curtime + gpGlobals->frametime * FRAMES_PER_THINK, "FlyThinkContext" );
	if ( !pPhysObject )
		pPhysObject = VPhysicsGetObject();

	pPhysObject->GetVelocity( &vVelocity, NULL );
	fMoveDist += ( vVelocity * gpGlobals->frametime * FRAMES_PER_THINK ).Length();

	//if the portal has moved past the max distance, cancel it
	if ( fMoveDist > MAX_MOVEDIST )
	{
		CProp_Portal *pPortal = CProp_Portal::FindPortal( iLinkageGroup, bPortal2, true );

		pPortal->m_iDelayedFailure = PORTAL_FIZZLE_NONE;
		VectorAngles( -vVelocity, pPortal->m_qDelayedAngles );
		pPortal->m_vDelayedPosition = GetAbsOrigin();

		SetContextThink( NULL, TICK_NEVER_THINK, "FlyThinkContext" );

		pPortal->PlacePortal( pPortal->m_vDelayedPosition, pPortal->m_qDelayedAngles, PORTAL_ANALOG_SUCCESS_PASSTHROUGH_SURFACE );

		pPortal->m_vDelayedPosition = GetAbsOrigin();
		pPortal->m_hPlacedBy = this;
		pPortal->SetContextThink( &CProp_Portal::PlacementThink, gpGlobals->curtime, s_pDelayedPlacementContext );
		UTIL_Remove( this );
	}

	if ( bHitObject && !PhysIsInCallback() )
	{
		CPortalSimulator *pSimulator = CPortalSimulator::GetSimulatorThatOwnsEntity( this );
		if ( pSimulator )
		{
			pSimulator->ReleaseOwnershipOfEntity( this );
		}
		SetContextThink( NULL, TICK_NEVER_THINK, "FlyThinkContext" );
		UTIL_Remove( this ); //delete object if hit and not in callback
	}
}

void CFlyingPortal::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	DevMsg( "Used\n" );
	CBasePlayer *pPlayer = ToBasePlayer( pActivator );
	if ( pPlayer )
	{
		pPlayer->PickupObject( this, false );
	}
}

void CFlyingPortal::StartTouch( CBaseEntity *pOther )
{
	Msg( "Classic collision with " );
	Msg( pOther->GetClassname() );
	Msg( "\n" );
	if ( FClassnameIs( pOther, "trigger_portal_cleanser" ) )
	{
		CBaseTrigger *pTrigger = static_cast<CBaseTrigger*>( pOther );
		Msg( "portal hit portal cleanser\n" );

		if ( pTrigger && !pTrigger->m_bDisabled )
		{
			CProp_Portal *pPortal = CProp_Portal::FindPortal( iLinkageGroup, bPortal2, true );

			pPortal->m_iDelayedFailure = PORTAL_FIZZLE_CLEANSER;
			VectorAngles( -vVelocity, pPortal->m_qDelayedAngles );
			pPortal->m_vDelayedPosition = GetAbsOrigin();

			bHitObject = true;
			return;
		}
	}
}

void CFlyingPortal::VPhysicsCollision( int index, gamevcollisionevent_t *pEvent )
{
	if ( bHitObject )
		return;

	Vector CurrentVelocity;
	pPhysObject->GetVelocity( &CurrentVelocity, NULL );
	if ( CurrentVelocity.LengthSqr() == 0 )
		return; //portal has not yet started moving - don't interfere w/ it

	CBaseEntity *pOther = pEvent->pEntities[!index];

	Msg( "Vphysics collision with " );
	Msg( pOther->GetClassname() );
	Msg( "\n" );

	if ( pOther->IsPlayer() )
		return;

	Vector vCollisionNormal, vCollisionPoint;
	pEvent->pInternalData->GetSurfaceNormal( vCollisionNormal );
	pEvent->pInternalData->GetContactPoint( vCollisionPoint );
	vCollisionNormal = -vCollisionNormal;

	/*
	if ( !IsEntityPortalable( pOther ) )
	{
		BaseClass::VPhysicsCollision( index, pEvent ); //bounce
		return; //portal can't be put on this object
	}
	*/

	if ( bHitObject )
		return; //portal already placed, waiting for think to remove

	float fSuccess = 1.0f;

	if ( FClassnameIs( pOther, "trigger_portal_cleanser" ) )
	{
		CBaseTrigger *pTrigger = static_cast<CBaseTrigger*>( pOther );
		Msg( "portal hit portal cleanser\n" );

		if ( pTrigger && !pTrigger->m_bDisabled )
		{
			CProp_Portal *pPortal = CProp_Portal::FindPortal( iLinkageGroup, bPortal2, true );

			pPortal->m_iDelayedFailure = PORTAL_FIZZLE_CLEANSER;
			VectorAngles( -vVelocity, pPortal->m_qDelayedAngles );
			pPortal->m_vDelayedPosition = GetAbsOrigin();

			fSuccess = PORTAL_ANALOG_SUCCESS_CLEANSER;
			bHitObject = true;
			return;
		}
	}

	Vector vUp( 0.0f, 0.0f, 1.0f );
	if ( ( vCollisionNormal.x > -0.001f && vCollisionNormal.x < 0.001f ) && ( vCollisionNormal.y > -0.001f && vCollisionNormal.y < 0.001f ) )
	{
		//plane is a level floor/ceiling
		vUp = CurrentVelocity.Normalized();
	}
	Vector Origin = vCollisionPoint;
	QAngle Angles;
	VectorAngles( vCollisionNormal, vUp, Angles );
	if ( fSuccess == 1.0f ) //if not already failed
		fSuccess = VerifyPortalPlacement( CProp_Portal::FindPortal( iLinkageGroup, bPortal2 ), Origin, Angles, PORTAL_PLACED_BY_PLAYER, false );

	ConVar *sv_portal_placement_never_fail = cvar->FindVar( "sv_portal_placement_never_fail" );
	if ( sv_portal_placement_never_fail->GetBool() )
	{
		fSuccess = 1.0f;
	}

	if ( fSuccess < .5f )
	{
		BaseClass::VPhysicsCollision( index, pEvent ); //bounce
		return; //portal placement failed
	}


	CProp_Portal *pPortal = CProp_Portal::FindPortal( iLinkageGroup, bPortal2, true );

	pPortal->m_iDelayedFailure = PORTAL_FIZZLE_NONE;
	pPortal->m_qDelayedAngles = Angles;
	pPortal->m_vDelayedPosition = Origin;


	pPortal->PlacePortal( pPortal->m_vDelayedPosition, pPortal->m_qDelayedAngles, fSuccess );

	pPortal->m_hPlacedBy = this;
	pPortal->SetContextThink( &CProp_Portal::PlacementThink, gpGlobals->curtime, s_pDelayedPlacementContext );

	bHitObject = true;
}

static char *g_pszPortalable[] =
{
	"func_physbox",
	"func_movelinear",
	"func_tracktrain",
	"func_platrot",
	"func_brush"
};

bool IsEntityPortalable( CBaseEntity *pEnt )
{
	if ( pEnt->IsWorld() )
		return true;

	for ( int i = 0; i < ARRAYSIZE( g_pszPortalable ); ++i )
		if ( FClassnameIs( pEnt, g_pszPortalable[i] ) )
			return true;


	return false;
}
