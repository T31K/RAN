/*-----------------------------------------------------------------------------
 view_ChaFriend
CREATE VIEW dbo.viewChaFriend
AS
SELECT     TOP (100) PERCENT dbo.ChaFriend.ChaP, dbo.ChaFriend.ChaS, dbo.ChaInfo.ChaName, dbo.ChaFriend.ChaFlag
FROM         dbo.ChaFriend INNER JOIN
                      dbo.ChaInfo ON dbo.ChaFriend.ChaS = dbo.ChaInfo.ChaNum
ORDER BY dbo.ChaInfo.ChaName

