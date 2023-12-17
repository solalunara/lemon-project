#include "cbase.h"
#include "C_FlyingPortal.h"

LINK_ENTITY_TO_CLASS( flying_portal, C_FlyingPortal );

IMPLEMENT_CLIENTCLASS_DT( C_FlyingPortal, DT_FlyingPortal, CFlyingPortal )
	RecvPropFloat( RECVINFO( fMoveDist ) ),
	RecvPropInt( RECVINFO( iLinkageGroup ) ),
	RecvPropBool( RECVINFO( bPortal2 ) ),
END_RECV_TABLE()


C_FlyingPortal::C_FlyingPortal()
{
}


C_FlyingPortal::~C_FlyingPortal()
{
}

void C_FlyingPortal::Spawn( void )
{
	SetThink( &C_FlyingPortal::ClientThink );
	SetNextClientThink( CLIENT_THINK_ALWAYS );

	BaseClass::Spawn();
}
void C_FlyingPortal::Activate( void )
{
	BaseClass::Activate();
}
void C_FlyingPortal::ClientThink( void )
{
	BaseClass::ClientThink();
}

void C_FlyingPortal::Simulate()
{
	BaseClass::Simulate();
}

void C_FlyingPortal::UpdateOnRemove( void )
{
	BaseClass::UpdateOnRemove();
}

void C_FlyingPortal::OnNewParticleEffect( const char *pszParticleName, CNewParticleEffect *pNewParticleEffect )
{
	BaseClass::OnNewParticleEffect( pszParticleName, pNewParticleEffect );
}

void C_FlyingPortal::OnPreDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnPreDataChanged( updateType );
}
void C_FlyingPortal::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );
}
int	C_FlyingPortal::DrawModel( int flags )
{
	return BaseClass::DrawModel( flags );
}
