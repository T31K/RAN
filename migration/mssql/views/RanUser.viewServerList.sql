CREATE VIEW [dbo].[viewServerList]
AS
SELECT A.SGNum, A.SvrNum, A.SvrType, B.SGName, B.OdbcName, B.OdbcUserID, 
      B.OdbcPassword, B.OdbcLogName, B.OdbcLogUserID, B.OdbcLogPassword
FROM dbo.ServerInfo A LEFT OUTER JOIN
      dbo.ServerGroup B ON A.SGNum = B.SGNum
WHERE (A.SvrType = 4)

