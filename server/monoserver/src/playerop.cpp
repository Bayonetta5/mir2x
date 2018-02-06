/*
 * =====================================================================================
 *
 *       Filename: playerop.cpp
 *        Created: 05/11/2016 17:37:54
 *    Description:
 *
 *        Version: 1.0
 *       Revision: none
 *       Compiler: gcc
 *
 *         Author: ANHONG
 *          Email: anhonghe@gmail.com
 *   Organization: USTC
 *
 * =====================================================================================
 */

#include <cinttypes>
#include "player.hpp"
#include "dbcomid.hpp"
#include "memorypn.hpp"
#include "actorpod.hpp"
#include "netdriver.hpp"
#include "monoserver.hpp"

void Player::On_MPK_METRONOME(const MessagePack &, const Theron::Address &)
{
    extern NetDriver *g_NetDriver;
    extern MonoServer *g_MonoServer;

    Update();

    SMPing stSMP;
    stSMP.Tick = g_MonoServer->GetTimeTick();
    g_NetDriver->Post(ChannID(), SM_PING, stSMP);
}

void Player::On_MPK_BINDCHANNEL(const MessagePack &rstMPK, const Theron::Address &)
{
    AMBindChannel stAMBC;
    std::memcpy(&stAMBC, rstMPK.Data(), sizeof(stAMBC));

    // bind channel here
    // set the channel actor as this->GetAddress()
    m_ChannID = stAMBC.ChannID;

    extern NetDriver *g_NetDriver;
    g_NetDriver->BindActor(ChannID(), GetAddress());

    SMLoginOK stSMLOK;
    std::memset(&stSMLOK, 0, sizeof(stSMLOK));

    stSMLOK.UID       = UID();
    stSMLOK.DBID      = DBID();
    stSMLOK.MapID     = m_Map->ID();
    stSMLOK.X         = X();
    stSMLOK.Y         = Y();
    stSMLOK.Male      = true;
    stSMLOK.Direction = Direction();
    stSMLOK.JobID     = JobID();
    stSMLOK.Level     = Level();

    extern NetDriver *g_NetDriver;
    g_NetDriver->Post(ChannID(), SM_LOGINOK, stSMLOK);

    PullRectCO(10, 10);
}

void Player::On_MPK_NETPACKAGE(const MessagePack &rstMPK, const Theron::Address &)
{
    AMNetPackage stAMNP;
    std::memcpy(&stAMNP, rstMPK.Data(), sizeof(AMNetPackage));

    uint8_t *pDataBuf = nullptr;
    if(stAMNP.DataLen){
        if(stAMNP.Data){
            pDataBuf = stAMNP.Data;
        }else{
            pDataBuf = stAMNP.DataBuf;
        }
    }

    OperateNet(stAMNP.Type, pDataBuf, stAMNP.DataLen);

    if(stAMNP.Data){
        delete [] stAMNP.Data;
    }
}

void Player::On_MPK_ACTION(const MessagePack &rstMPK, const Theron::Address &)
{
    AMAction stAMA;
    std::memcpy(&stAMA, rstMPK.Data(), sizeof(stAMA));

    if(true
            && stAMA.UID != UID()
            && stAMA.MapID == MapID()
            && LDistance2(stAMA.X, stAMA.Y, X(), Y()) < 400){

        // for all types of action node
        // the x and y are always well-defined

        int nDirection = -1;
        switch(stAMA.Action){
            case ACTION_STAND:
            case ACTION_ATTACK:
            case ACTION_HITTED:
                {
                    nDirection = stAMA.Direction;
                    break;
                }
            default:
                {
                    break;
                }
        }

        extern MonoServer *g_MonoServer;
        m_LocationList[stAMA.UID] = COLocation
        {
            stAMA.UID,
            stAMA.MapID,
            g_MonoServer->GetTimeTick(),
            stAMA.X,
            stAMA.Y,
            nDirection
        };

        ReportAction(stAMA.UID, ActionNode
        {
            stAMA.Action,
            stAMA.Speed,
            stAMA.Direction,

            stAMA.X,
            stAMA.Y,
            stAMA.AimX,
            stAMA.AimY,
            stAMA.AimUID,
            stAMA.ActionParam,
        });
    }
}

void Player::On_MPK_NOTIFYNEWCO(const MessagePack &rstMPK, const Theron::Address &)
{
    AMNotifyNewCO stAMNNCO;
    std::memcpy(&stAMNNCO, rstMPK.Data(), sizeof(stAMNNCO));

    extern MonoServer *g_MonoServer;
    if(auto stUIDRecord = g_MonoServer->GetUIDRecord(stAMNNCO.UID)){
        switch(GetState(STATE_DEAD)){
            case 0:
                {
                    // should make an valid action node and send it
                    // currently just dispatch through map

                    DispatchAction(true, ActionStand(X(), Y(), Direction()));
                    break;
                }
            default:
                {
                    AMNotifyDead stAMND;

                    stAMND.UID = UID();
                    m_ActorPod->Forward({MPK_NOTIFYDEAD, stAMND}, stUIDRecord.GetAddress());
                    break;
                }
        }
    }
}

void Player::On_MPK_QUERYCORECORD(const MessagePack &rstMPK, const Theron::Address &)
{
    AMQueryCORecord stAMQCOR;
    std::memcpy(&stAMQCOR, rstMPK.Data(), sizeof(stAMQCOR));

    ReportCORecord(stAMQCOR.UID);
}

void Player::On_MPK_MAPSWITCH(const MessagePack &rstMPK, const Theron::Address &)
{
    AMMapSwitch stAMMS;
    std::memcpy(&stAMMS, rstMPK.Data(), sizeof(stAMMS));

    if(stAMMS.UID && stAMMS.MapID){
        extern MonoServer *g_MonoServer;
        if(auto stUIDRecord = g_MonoServer->GetUIDRecord(stAMMS.UID)){
            AMTryMapSwitch stAMTMS;
            stAMTMS.UID    = UID();         //
            stAMTMS.MapID  = m_Map->ID();   // current map
            stAMTMS.MapUID = m_Map->UID();  // current map
            stAMTMS.X      = X();           // current map
            stAMTMS.Y      = Y();           // current map
            stAMTMS.EndX   = stAMMS.X;      // map to switch to
            stAMTMS.EndY   = stAMMS.Y;      // map to switch to

            // 1. send request to the new map
            //    if request rejected then it stays in current map
            auto fnOnResp = [this](const MessagePack &rstRMPK, const Theron::Address &)
            {
                switch(rstRMPK.Type()){
                    case MPK_MAPSWITCHOK:
                        {
                            // new map permit this switch request
                            // new map will guarante to outlive current object
                            AMMapSwitchOK stAMMSOK;
                            std::memcpy(&stAMMSOK, rstRMPK.Data(), sizeof(stAMMSOK));
                            if(true
                                    && ((ServerMap *)(stAMMSOK.Data))
                                    && ((ServerMap *)(stAMMSOK.Data))->ID()
                                    && ((ServerMap *)(stAMMSOK.Data))->UID()
                                    && ((ServerMap *)(stAMMSOK.Data))->ValidC(stAMMSOK.X, stAMMSOK.Y)){

                                AMTryLeave stAMTL;
                                stAMTL.UID   = UID();
                                stAMTL.MapID = m_Map->ID();
                                stAMTL.X     = X();
                                stAMTL.Y     = Y();

                                // current map respond for the leave request
                                // dangerous here, we should keep m_Map always valid
                                auto fnOnLeaveResp = [this, stAMMSOK, rstRMPK](const MessagePack &rstLeaveRMPK, const Theron::Address &)
                                {
                                    switch(rstLeaveRMPK.Type()){
                                        case MPK_OK:
                                            {
                                                // 1. response to new map ``I am here"
                                                m_Map   = (ServerMap *)(stAMMSOK.Data);
                                                m_X = stAMMSOK.X;
                                                m_Y = stAMMSOK.Y;
                                                m_ActorPod->Forward(MPK_OK, m_Map->GetAddress(), rstRMPK.ID());

                                                // 2. notify all players on the new map
                                                DispatchAction(true, ActionStand(X(), Y(), Direction()));

                                                // 3. inform the client for map swith
                                                ReportStand();

                                                // 4. pull all co's on the new map
                                                PullRectCO(10, 10);
                                                break;
                                            }
                                        default:
                                            {
                                                // can't leave???, illegal response
                                                // server map won't respond any other message not MPK_OK
                                                // dangerous issue since we then can never inform the new map ``we can't come to you"
                                                m_ActorPod->Forward(MPK_ERROR, ((ServerMap *)(stAMMSOK.Data))->GetAddress(), rstRMPK.ID());

                                                extern MonoServer *g_MonoServer;
                                                g_MonoServer->AddLog(LOGTYPE_WARNING, "Leave request failed: (UID = %" PRIu32 ", MapID = %" PRIu32 ")", UID(), ((ServerMap *)(stAMMSOK.Data))->ID());
                                                break;
                                            }
                                    }
                                };
                                m_ActorPod->Forward({MPK_TRYLEAVE, stAMTL}, m_Map->GetAddress(), fnOnLeaveResp);
                                return;
                            }

                            // AMMapSwitchOK invalid
                            extern MonoServer *g_MonoServer;
                            g_MonoServer->AddLog(LOGTYPE_WARNING, "Invalid AMMapSwitchOK: Map = %p", stAMMSOK.Data);
                            return;
                        }
                    default:
                        {
                            // do nothing
                            // new map reject this switch request
                            return;
                        }
                }
            };
            m_ActorPod->Forward({MPK_TRYMAPSWITCH, stAMTMS}, stUIDRecord.GetAddress(), fnOnResp);
            return;
        }
    }

    extern MonoServer *g_MonoServer;
    g_MonoServer->AddLog(LOGTYPE_WARNING, "Map switch request failed: (UID = %" PRIu32 ", MapID = %" PRIu32 ")", stAMMS.UID, stAMMS.MapID);
}

void Player::On_MPK_QUERYLOCATION(const MessagePack &rstMPK, const Theron::Address &rstFromAddr)
{
    AMLocation stAML;
    stAML.UID       = UID();
    stAML.MapID     = MapID();
    stAML.X         = X();
    stAML.Y         = Y();
    stAML.Direction = Direction();

    m_ActorPod->Forward({MPK_LOCATION, stAML}, rstFromAddr, rstMPK.ID());
}

void Player::On_MPK_ATTACK(const MessagePack &rstMPK, const Theron::Address &)
{
    AMAttack stAMA;
    std::memcpy(&stAMA, rstMPK.Data(), sizeof(stAMA));

    DispatchAction(false, ActionHitted(X(), Y(), Direction()));
    StruckDamage({stAMA.UID, stAMA.Type, stAMA.Damage, stAMA.Element});

    ReportAction(UID(), ActionHitted(X(), Y(), Direction()));
}

void Player::On_MPK_UPDATEHP(const MessagePack &rstMPK, const Theron::Address &)
{
    AMUpdateHP stAMUHP;
    std::memcpy(&stAMUHP, rstMPK.Data(), sizeof(stAMUHP));

    if(stAMUHP.UID != UID()){
        SMUpdateHP stSMUHP;
        stSMUHP.UID   = stAMUHP.UID;
        stSMUHP.MapID = stAMUHP.MapID;
        stSMUHP.HP    = stAMUHP.HP;
        stSMUHP.HPMax = stAMUHP.HPMax;

        extern NetDriver *g_NetDriver;
        g_NetDriver->Post(ChannID(), SM_UPDATEHP, stSMUHP);
    }
}

void Player::On_MPK_DEADFADEOUT(const MessagePack &rstMPK, const Theron::Address &)
{
    AMDeadFadeOut stAMDFO;
    std::memcpy(&stAMDFO, rstMPK.Data(), sizeof(stAMDFO));

    if(stAMDFO.UID != UID()){
        SMDeadFadeOut stSMDFO;
        stSMDFO.UID   = stAMDFO.UID;
        stSMDFO.MapID = stAMDFO.MapID;
        stSMDFO.X     = stAMDFO.X;
        stSMDFO.Y     = stAMDFO.Y;

        extern NetDriver *g_NetDriver;
        g_NetDriver->Post(ChannID(), SM_DEADFADEOUT, stSMDFO);
    }
}

void Player::On_MPK_EXP(const MessagePack &rstMPK, const Theron::Address &)
{
    AMExp stAME;
    std::memcpy(&stAME, rstMPK.Data(), sizeof(stAME));

    GainExp(stAME.Exp);

    if(stAME.Exp > 0){
        SMExp stSME;
        stSME.Exp = stAME.Exp;

        extern NetDriver *g_NetDriver;
        g_NetDriver->Post(ChannID(), SM_EXP, stSME);
    }
}

void Player::On_MPK_SHOWDROPITEM(const MessagePack &rstMPK, const Theron::Address &)
{
    AMShowDropItem stAMSDI;
    std::memcpy(&stAMSDI, rstMPK.Data(), sizeof(stAMSDI));

    SMShowDropItem stSMSDI;
    std::memset(&stSMSDI, 0, sizeof(stSMSDI));

    stSMSDI.X  = stAMSDI.X;
    stSMSDI.Y  = stAMSDI.Y;

    constexpr auto nSMIDListLen = std::extent<decltype(stSMSDI.IDList)>::value;
    constexpr auto nAMIDListLen = std::extent<decltype(stAMSDI.IDList)>::value;

    static_assert(nSMIDListLen >= nAMIDListLen, "");
    for(size_t nIndex = 0; nIndex < nAMIDListLen; ++nIndex){
        if(stAMSDI.IDList[nIndex].ID){
            stSMSDI.IDList[nIndex].ID   = stAMSDI.IDList[nIndex].ID;
            stSMSDI.IDList[nIndex].DBID = stAMSDI.IDList[nIndex].DBID;
        }else{
            break;
        }
    }

    extern NetDriver *g_NetDriver;
    g_NetDriver->Post(ChannID(), SM_SHOWDROPITEM, stSMSDI);
}

void Player::On_MPK_BADCHANNEL(const MessagePack &rstMPK, const Theron::Address &)
{
    AMBadChannel stAMBC;
    std::memcpy(&stAMBC, rstMPK.Data(), sizeof(stAMBC));

    condcheck(ChannID() == stAMBC.ChannID);

    extern NetDriver *g_NetDriver;
    g_NetDriver->Shutdown(ChannID(), false);

    Offline();
}

void Player::On_MPK_OFFLINE(const MessagePack &rstMPK, const Theron::Address &)
{
    AMOffline stAMO;
    std::memcpy(&stAMO, rstMPK.Data(), sizeof(stAMO));

    ReportOffline(stAMO.UID, stAMO.MapID);
}

void Player::On_MPK_REMOVEGROUNDITEM(const MessagePack &rstMPK, const Theron::Address &)
{
    AMRemoveGroundItem stAMRGI;
    std::memcpy(&stAMRGI, rstMPK.Data(), sizeof(stAMRGI));

    SMRemoveGroundItem stSMRGI;
    stSMRGI.X    = stAMRGI.X;
    stSMRGI.Y    = stAMRGI.Y;
    stSMRGI.ID   = stAMRGI.ID;
    stSMRGI.DBID = stAMRGI.DBID;

    PostNetMessage(SM_REMOVEGROUNDITEM, stSMRGI);
}

void Player::On_MPK_PICKUPOK(const MessagePack &rstMPK, const Theron::Address &)
{
    AMPickUpOK stAMPUOK;
    std::memcpy(&stAMPUOK, rstMPK.Data(), sizeof(stAMPUOK));

    SMPickUpOK stSMPUOK;
    std::memset(&stSMPUOK, 0, sizeof(stSMPUOK));

    stSMPUOK.X    = stAMPUOK.X;
    stSMPUOK.Y    = stAMPUOK.Y;
    stSMPUOK.ID   = stAMPUOK.ID;
    stSMPUOK.DBID = stAMPUOK.DBID;

    PostNetMessage(SM_PICKUPOK, stSMPUOK);

    switch(stAMPUOK.ID){
        case DBCOM_ITEMID(u8"金币"):
            {
                m_Gold += std::rand() % 500;
                ReportGold();
                break;
            }
        default:
            {
                m_Inventory.emplace_back(stAMPUOK.ID, stAMPUOK.DBID);
                break;
            }
    }

}

void Player::On_MPK_CORECORD(const MessagePack &rstMPK, const Theron::Address &)
{
    AMCORecord stAMCOR;
    std::memcpy(&stAMCOR, rstMPK.Data(), sizeof(stAMCOR));

    SMCORecord stSMCOR;
    std::memset(&stSMCOR, 0, sizeof(stSMCOR));

    // 1. set type
    stSMCOR.COType = stAMCOR.COType;

    // 2. set common info
    stSMCOR.Action.UID   = stAMCOR.Action.UID;
    stSMCOR.Action.MapID = stAMCOR.Action.MapID;

    stSMCOR.Action.Action    = stAMCOR.Action.Action;
    stSMCOR.Action.Speed     = stAMCOR.Action.Speed;
    stSMCOR.Action.Direction = stAMCOR.Action.Direction;

    stSMCOR.Action.X    = stAMCOR.Action.X;
    stSMCOR.Action.Y    = stAMCOR.Action.Y;
    stSMCOR.Action.AimX = stAMCOR.Action.AimX;
    stSMCOR.Action.AimY = stAMCOR.Action.AimY;

    stSMCOR.Action.AimUID      = stAMCOR.Action.AimUID;
    stSMCOR.Action.ActionParam = stAMCOR.Action.ActionParam;

    // 3. set specified info
    switch(stAMCOR.COType){
        case CREATURE_PLAYER:
            {
                stSMCOR.Player.DBID  = stAMCOR.Player.DBID;
                stSMCOR.Player.JobID = stAMCOR.Player.JobID;
                stSMCOR.Player.Level = stAMCOR.Player.Level;
                break;
            }
        case CREATURE_MONSTER:
            {
                stSMCOR.Monster.MonsterID = stAMCOR.Monster.MonsterID;
                break;
            }
        default:
            {
                break;
            }
    }

    PostNetMessage(SM_CORECORD, stSMCOR);
}

void Player::On_MPK_NOTIFYDEAD(const MessagePack &, const Theron::Address &)
{
}
