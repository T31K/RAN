
-- view_GuildMember.sql

CREATE VIEW [dbo].[viewGuildMember]
AS
SELECT   dbo.GuildInfo.GuNum, dbo.ChaInfo.ChaNum, dbo.ChaInfo.ChaName, 
         dbo.ChaInfo.ChaGuName, dbo.ChaInfo.GuPosition
FROM     dbo.GuildInfo INNER JOIN
         dbo.ChaInfo ON dbo.GuildInfo.GuNum = dbo.ChaInfo.GuNum
WHERE   (dbo.GuildInfo.GuExpire = 0) AND (dbo.ChaInfo.ChaDeleted = 0)

