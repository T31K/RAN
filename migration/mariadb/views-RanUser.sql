USE RanUser;
DROP VIEW IF EXISTS ShopPurchase;
CREATE VIEW ShopPurchase AS SELECT *
FROM RanShop.ShopPurchase;
DROP VIEW IF EXISTS XF_UserPoint;
CREATE VIEW XF_UserPoint AS SELECT UserInfo.UserNum, UserInfo.UserName, UserInfo.UserID, 
      UserInfo.NewUserPass1, UserInfo.NewUserPass, UserPoint.Point
FROM UserInfo INNER JOIN
      UserPoint ON UserInfo.UserNum = UserPoint.UserNum;
DROP VIEW IF EXISTS viewServerList;
CREATE VIEW viewServerList AS SELECT A.SGNum, A.SvrNum, A.SvrType, B.SGName, B.OdbcName, B.OdbcUserID, 
      B.OdbcPassword, B.OdbcLogName, B.OdbcLogUserID, B.OdbcLogPassword
FROM ServerInfo A LEFT OUTER JOIN
      ServerGroup B ON A.SGNum = B.SGNum
WHERE (A.SvrType = 4);
