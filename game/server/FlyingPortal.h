#ifndef FLYINGPORTAL_H
#define FLYINGPORTAL_H

#pragma once
#include "baseanimating.h"
#include "portal_physics_collisionevent.h"

class CFlyingPortal :
	public CBaseAnimating
{
public:
	DECLARE_CLASS( CFlyingPortal, CBaseAnimating );
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

				CFlyingPortal();
	virtual		~CFlyingPortal( void );

	static CFlyingPortal	*Create( Vector vStartPos, Vector vDirection, float fSpeed, int iLinkageGroup, bool bPortal2 );

	virtual void			UpdateOnRemove( void );
	virtual void			Precache( void );
	virtual void			StopLoopingSounds( void );
	virtual void			Spawn( void );
	virtual void			Activate( void );
	virtual void			OnRestore( void );

	void					Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	virtual void			VPhysicsCollision( int index, gamevcollisionevent_t *pEvent );

	virtual int				UpdateTransmitState( void )	// set transmit filter to transmit always
	{
		return SetTransmitState( FL_EDICT_ALWAYS );
	}

	void					FlyThink( void );

	CNetworkVar( float, fMoveDist );
	CNetworkVar( int, iLinkageGroup );
	CNetworkVar( bool, bPortal2 );

	Vector			vVelocity;
	QAngle			qAngles;

	bool			bHitObject;

	IPhysicsObject	*pPhysObject;
	IPhysicsEnvironment *pPhysEnvironment;

	CPortal_CollisionEvent g_CollisionHandler;
};

bool IsEntityPortalable( CBaseEntity *pEnt );

#endif
