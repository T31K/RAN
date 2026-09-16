USE RanGame1;
DROP VIEW IF EXISTS viewChaFriend;
CREATE VIEW viewChaFriend AS SELECT     ChaFriend.ChaP, ChaFriend.ChaS, ChaInfo.ChaName, ChaFriend.ChaFlag
FROM         ChaFriend INNER JOIN
                      ChaInfo ON ChaFriend.ChaS = ChaInfo.ChaNum
ORDER BY ChaInfo.ChaName;
DROP VIEW IF EXISTS viewGuildInfo;
CREATE VIEW viewGuildInfo AS SELECT     GuildInfo.GuNum, GuildInfo.ChaNum, GuildInfo.GuDeputy, ChaInfo.ChaName, GuildInfo.GuName, GuildInfo.GuNotice, 
                      GuildInfo.GuRank, GuildInfo.GuMoney, GuildInfo.GuIncomeMoney, GuildInfo.GuMarkVer, GuildInfo.GuExpire, 
                      GuildInfo.GuMakeTime, GuildInfo.GuExpireTime, GuildInfo.GuAllianceSec, GuildInfo.GuAllianceDis, GuildInfo.GuAuthorityTime, 
                      GuildInfo.GuAllianceBattleLose, GuildInfo.GuAllianceBattleDraw, GuildInfo.GuAllianceBattleWin, GuildInfo.GuBattleLastTime, 
                      GuildInfo.GuBattleLose, GuildInfo.GuBattleDraw, GuildInfo.GuBattleWin, ChaInfo.ChaSpMID, ChaInfo.ChaSpSID
FROM         GuildInfo INNER JOIN
                      ChaInfo ON GuildInfo.ChaNum = ChaInfo.ChaNum;
DROP VIEW IF EXISTS viewGuildMember;
CREATE VIEW viewGuildMember AS SELECT   GuildInfo.GuNum, ChaInfo.ChaNum, ChaInfo.ChaName, 
         ChaInfo.ChaGuName, ChaInfo.GuPosition
FROM     GuildInfo INNER JOIN
         ChaInfo ON GuildInfo.GuNum = ChaInfo.GuNum
WHERE   (GuildInfo.GuExpire = 0) AND (ChaInfo.ChaDeleted = 0);
