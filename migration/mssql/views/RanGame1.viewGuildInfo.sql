/*-----------------------------------------------------------------------------
 view_GuildInfo.sql
CREATE VIEW [dbo].[viewGuildInfo]
AS
SELECT     dbo.GuildInfo.GuNum, dbo.GuildInfo.ChaNum, dbo.GuildInfo.GuDeputy, dbo.ChaInfo.ChaName, dbo.GuildInfo.GuName, dbo.GuildInfo.GuNotice, 
                      dbo.GuildInfo.GuRank, dbo.GuildInfo.GuMoney, dbo.GuildInfo.GuIncomeMoney, dbo.GuildInfo.GuMarkVer, dbo.GuildInfo.GuExpire, 
                      dbo.GuildInfo.GuMakeTime, dbo.GuildInfo.GuExpireTime, dbo.GuildInfo.GuAllianceSec, dbo.GuildInfo.GuAllianceDis, dbo.GuildInfo.GuAuthorityTime, 
                      dbo.GuildInfo.GuAllianceBattleLose, dbo.GuildInfo.GuAllianceBattleDraw, dbo.GuildInfo.GuAllianceBattleWin, dbo.GuildInfo.GuBattleLastTime, 
                      dbo.GuildInfo.GuBattleLose, dbo.GuildInfo.GuBattleDraw, dbo.GuildInfo.GuBattleWin, dbo.ChaInfo.ChaSpMID, dbo.ChaInfo.ChaSpSID
FROM         dbo.GuildInfo INNER JOIN
                      dbo.ChaInfo ON dbo.GuildInfo.ChaNum = dbo.ChaInfo.ChaNum



