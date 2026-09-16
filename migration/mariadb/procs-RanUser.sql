-- RanUser stored procedures ported MSSQL T-SQL -> MariaDB (provider 3 / default family)
-- Params prefixed p_ to avoid MariaDB case-insensitive collisions with column names.
USE RanUser;

DROP PROCEDURE IF EXISTS UserLogoutSimple;
DROP PROCEDURE IF EXISTS UserLogoutSimple2;
DROP PROCEDURE IF EXISTS UpdateChaName;
DROP PROCEDURE IF EXISTS user_verify;
DROP PROCEDURE IF EXISTS user_cha_test_remain;
DROP PROCEDURE IF EXISTS user_cha_remain;
DROP PROCEDURE IF EXISTS user_gettype;
DROP PROCEDURE IF EXISTS UpdateChaNumDecrease;
DROP PROCEDURE IF EXISTS UpdateChaNumIncrease;
DROP PROCEDURE IF EXISTS UpdateTestChaNumDecrease;
DROP PROCEDURE IF EXISTS UpdateTestChaNumIncrease;
DROP PROCEDURE IF EXISTS user_logout;

DELIMITER $$

CREATE PROCEDURE UserLogoutSimple(IN p_szUserID VARCHAR(20))
BEGIN
  UPDATE UserInfo SET UserLoginState=0 WHERE UserID=p_szUserID;
END$$

CREATE PROCEDURE UserLogoutSimple2(IN p_nUserNum INT)
BEGIN
  UPDATE UserInfo SET UserLoginState=0 WHERE UserNum=p_nUserNum;
END$$

CREATE PROCEDURE UpdateChaName(IN p_nUserNum INT, IN p_szChaName VARCHAR(33))
BEGIN
  UPDATE UserInfo SET ChaName=p_szChaName WHERE UserNum=p_nUserNum;
END$$

CREATE PROCEDURE user_verify(
  IN p_userId CHAR(25), IN p_userPass CHAR(25), IN p_userIp CHAR(25),
  IN p_SvrGrpNum INT, IN p_SvrNum INT, IN p_proPass VARCHAR(6), IN p_proNum VARCHAR(2),
  OUT p_nReturn INT)
BEGIN
  DECLARE v_unum INT DEFAULT NULL;
  SELECT UserNum INTO v_unum FROM UserInfo WHERE UserID=p_userId LIMIT 1;
  IF v_unum IS NULL THEN
    INSERT INTO UserInfo (UserName, UserID, UserPass, UserPass2) VALUES (p_userId, p_userId, 'x', 'x');
    SET v_unum = LAST_INSERT_ID();
  END IF;
  UPDATE UserInfo SET UserLoginState=1, LastLoginDate=NOW(), SGNum=p_SvrGrpNum, SvrNum=p_SvrNum WHERE UserNum=v_unum;
  SET p_nReturn = 1;
END$$

CREATE PROCEDURE user_cha_test_remain(IN p_nUserNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_rem INT;
  SET p_nReturn = 0;
  SELECT ChaTestRemain INTO v_rem FROM UserInfo WHERE UserNum=p_nUserNum LIMIT 1;
  SET p_nReturn = v_rem;
END$$

CREATE PROCEDURE user_cha_remain(IN p_nUserNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_rem INT DEFAULT 0;
  SET p_nReturn = 0;
  SELECT ChaRemain INTO v_rem FROM UserInfo WHERE UserNum=p_nUserNum LIMIT 1;
  SET p_nReturn = v_rem;
END$$

CREATE PROCEDURE user_gettype(IN p_nUserNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_type INT DEFAULT 0;
  SET p_nReturn = 0;
  SELECT UserType INTO v_type FROM UserInfo WHERE UserNum=p_nUserNum LIMIT 1;
  SET p_nReturn = v_type;
END$$

CREATE PROCEDURE UpdateChaNumDecrease(IN p_nUserNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_rc INT DEFAULT 0;
  DECLARE v_err INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  UPDATE UserInfo SET ChaRemain=ChaRemain-1 WHERE UserNum=p_nUserNum;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN SET p_nReturn = -1; ELSE SET p_nReturn = 0; END IF;
END$$

CREATE PROCEDURE UpdateChaNumIncrease(IN p_nUserNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_rc INT DEFAULT 0;
  DECLARE v_err INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  UPDATE UserInfo SET ChaRemain=ChaRemain+1 WHERE UserNum=p_nUserNum;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN SET p_nReturn = -1; ELSE SET p_nReturn = 0; END IF;
END$$

CREATE PROCEDURE UpdateTestChaNumDecrease(IN p_nUserNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_rc INT DEFAULT 0;
  DECLARE v_err INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  UPDATE UserInfo SET ChaTestRemain=ChaTestRemain-1 WHERE UserNum=p_nUserNum;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN SET p_nReturn = -1; ELSE SET p_nReturn = 0; END IF;
END$$

CREATE PROCEDURE UpdateTestChaNumIncrease(IN p_nUserNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_rc INT DEFAULT 0;
  DECLARE v_err INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  UPDATE UserInfo SET ChaTestRemain=ChaTestRemain+1 WHERE UserNum=p_nUserNum;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN SET p_nReturn = -1; ELSE SET p_nReturn = 0; END IF;
END$$

CREATE PROCEDURE user_logout(
  IN p_userId CHAR(25), IN p_usernum INT, IN p_gametime INT, IN p_chanum INT,
  IN p_svrgrp INT, IN p_svrnum INT, IN p_totalgametime INT, IN p_offlinetime INT)
BEGIN
  DECLARE v_rc INT DEFAULT 0;
  UPDATE UserInfo SET UserLoginState=0, LastLoginDate=NOW(), GameTime=p_totalgametime, OfflineTime=p_offlinetime
    WHERE UserNum=p_usernum;
  INSERT INTO LogLogin (UserNum, UserID, LogInOut) VALUES (p_usernum, p_userId, 0);
  INSERT INTO LogGameTime (UserNum, UserID, GameTime, ChaNum, SGNum, SvrNum)
    VALUES (p_usernum, p_userId, p_gametime, p_chanum, p_svrgrp, p_svrnum);
  UPDATE UserInfo SET GameTime2=GameTime2+p_gametime WHERE UserNum=p_usernum;
  UPDATE StatGameTime SET GTime=GTime+p_gametime
    WHERE GYear=YEAR(NOW()) AND GMonth=MONTH(NOW()) AND GDay=DAY(NOW());
  SET v_rc = ROW_COUNT();
  IF v_rc = 0 THEN
    INSERT INTO StatGameTime (GYear, GMonth, GDay, GTime)
      VALUES (YEAR(NOW()), MONTH(NOW()), DAY(NOW()), p_gametime);
  END IF;
END$$

DELIMITER ;
