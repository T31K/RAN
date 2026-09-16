CREATE VIEW [dbo].[XF_UserPoint]
AS
SELECT dbo.UserInfo.UserNum, dbo.UserInfo.UserName, dbo.UserInfo.UserID, 
      dbo.UserInfo.NewUserPass1, dbo.UserInfo.NewUserPass, dbo.UserPoint.Point
FROM dbo.UserInfo INNER JOIN
      dbo.UserPoint ON dbo.UserInfo.UserNum = dbo.UserPoint.UserNum

