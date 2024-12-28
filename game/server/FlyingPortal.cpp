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
	DEFINE_THINKFUNC( FlyThink ),
	DEFINE_THINKFUNC( KillThink )
END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( CFlyingPortal, DT_FlyingPortal )
	SendPropFloat( SENDINFO( fMoveDist ) ),
	SendPropInt( SENDINFO( iLinkageGroup ) ),
	SendPropBool( SENDINFO( bPortal2 ) ),
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( flying_portal, CFlyingPortal );

#define FLYING_PORTAL_MINS Vector( -.2, -.2, -.2 )
#define FLYING_PORTAL_MAXS Vector( .2, .2, .2 )

ConVar sv_portal_delay_shot_time( "sv_portal_delay_shot_time", "0", FCVAR_CHEAT );


CFlyingPortal::CFlyingPortal() :
	CBaseAnimating(), bHitObject( false ), iNumBounces( 0 )
{
	RegisterThinkContext( "FlyThinkContext" );
}

CFlyingPortal *CFlyingPortal::Create( Vector vStartPos, Vector vDirection, float fSpeed, int iLinkageGroup, bool bPortal2 )
{
	QAngle Angles;
	VectorAngles( vDirection, Angles );
	CFlyingPortal *pPortal = static_cast<CFlyingPortal *>( BaseClass::Create( "flying_portal", vStartPos, Angles ) );
	pPortal->qAngles = Angles;
	pPortal->vVelocity = vDirection.Normalized() * fSpeed;
	pPortal->iLinkageGroup = iLinkageGroup;
	pPortal->bPortal2 = bPortal2;
	pPortal->SetContextThink( &CFlyingPortal::FlyThink, gpGlobals->curtime + sv_portal_delay_shot_time.GetFloat(), "FlyThinkContext" );
	pPortal->SetCollisionGroup( COLLISION_GROUP_PUSHAWAY );
	pPortal->CollisionRulesChanged();


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

void CFlyingPortal::StopLoopingSounds()
{
	BaseClass::StopLoopingSounds();
}

void CFlyingPortal::Spawn( void )
{
	SetModel( "models/props_bts/rocket.mdl" );
	SetCollisionBoundsFromModel();

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
	if ( bHitObject )
	{
		VPhysicsDestroyObject();
		SetTouch( NULL );
		SetSolid( SOLID_NONE );
		AddEffects( EF_NODRAW );
		SetContextThink( &CFlyingPortal::KillThink, gpGlobals->curtime + 2.0f, "FlyThinkContext" );
		return;
	}
	if ( !VPhysicsGetObject() )
	{
		IPhysicsObject *pPhysObject = VPhysicsInitNormal( SOLID_VPHYSICS, 0, false );
		pPhysObject->EnableGravity( false );
		pPhysObject->EnableCollisions( true );
		pPhysObject->SetMass( 2 );
		pPhysObject->EnableDrag( false );
		Vector inertia = Vector( 1, 10, 10 );
		pPhysObject->SetInertia( inertia );
		ApplyAbsVelocityImpulse( vVelocity );
		pPhysObject->RecheckCollisionFilter();
	}
	
	SetContextThink( &CFlyingPortal::FlyThink, gpGlobals->curtime + gpGlobals->frametime * FRAMES_PER_THINK, "FlyThinkContext" );

	VPhysicsGetObject()->GetVelocity( &vVelocity, NULL );
	fMoveDist += ( vVelocity * gpGlobals->frametime * FRAMES_PER_THINK ).Length();

	//if the portal has moved past the max distance, cancel it
	if ( fMoveDist > MAX_MOVEDIST || iNumBounces >= 3 )
	{
		CProp_Portal *pPortal = CProp_Portal::FindPortal( iLinkageGroup, bPortal2, true );

		pPortal->m_iDelayedFailure = PORTAL_FIZZLE_NONE;
		VectorAngles( -vVelocity, pPortal->m_qDelayedAngles );
		pPortal->m_vDelayedPosition = GetAbsOrigin();

		pPortal->PlacePortal( pPortal->m_vDelayedPosition, pPortal->m_qDelayedAngles, PORTAL_ANALOG_SUCCESS_INVALID_SURFACE, true );

		pPortal->m_vDelayedPosition = GetAbsOrigin();
		pPortal->m_hPlacedBy = this;
		pPortal->SetContextThink( &CProp_Portal::PlacementThink, gpGlobals->curtime, s_pDelayedPlacementContext );
		bHitObject = true;
	}

}

void CFlyingPortal::KillThink( void )
{
	Assert( !PhysIsInCallback() );
	CPortalSimulator *pSimulator = CPortalSimulator::GetSimulatorThatOwnsEntity( this );
	if ( pSimulator )
		pSimulator->ReleaseOwnershipOfEntity( this );
	SetContextThink( NULL, TICK_NEVER_THINK, "FlyThinkContext" );
	UTIL_Remove( this );
}

void CFlyingPortal::StartTouch( CBaseEntity *pOther )
{
	if ( FClassnameIs( pOther, "trigger_portal_cleanser" ) )
	{
		CBaseTrigger *pTrigger = static_cast<CBaseTrigger*>( pOther );

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

static CBaseEntity *s_pPreviousHitEntity = NULL;
void CFlyingPortal::VPhysicsCollision( int index, gamevcollisionevent_t *pEvent )
{
	if ( bHitObject )
		return;

	Vector CurrentVelocity;
	VPhysicsGetObject()->GetVelocity( &CurrentVelocity, NULL );

	CBaseEntity *pOther = pEvent->pEntities[!index];
	if ( pOther != s_pPreviousHitEntity && pEvent->deltaCollisionTime > 0.05f )
		iNumBounces++;


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


	pPortal->PlacePortal( pPortal->m_vDelayedPosition, pPortal->m_qDelayedAngles, fSuccess, true );

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
