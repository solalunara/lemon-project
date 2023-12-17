#ifndef C_FLYINGPORTAL_H
#define C_FLYINGPORTAL_H

#pragma once
#include "c_baseanimating.h"
class C_FlyingPortal :
	public C_BaseAnimating
{
public:
	DECLARE_CLASS( C_FlyingPortal, C_BaseAnimating );
	DECLARE_CLIENTCLASS();

					C_FlyingPortal();
	virtual			~C_FlyingPortal();

	virtual void			Spawn( void );
	virtual void			Activate( void );
	virtual void			ClientThink( void );

	virtual void			Simulate();

	virtual void			UpdateOnRemove( void );

	virtual void			OnNewParticleEffect( const char *pszParticleName, CNewParticleEffect *pNewParticleEffect );

	virtual void			OnPreDataChanged( DataUpdateType_t updateType );
	virtual void			OnDataChanged( DataUpdateType_t updateType );
	virtual int				DrawModel( int flags );

	float					fMoveDist;
	int						iLinkageGroup;
	bool					bPortal2;

};

#endif