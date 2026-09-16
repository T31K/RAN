-- RanGame1 stored procedures ported MSSQL T-SQL -> MariaDB (batch a)
-- Params prefixed p_ to avoid MariaDB case-insensitive collisions with column names.
USE RanGame1;

DROP PROCEDURE IF EXISTS UpdateUserMoney;
DROP PROCEDURE IF EXISTS InsertPartyMatch;
DROP PROCEDURE IF EXISTS UpdateAllCharacterOffline;
DROP PROCEDURE IF EXISTS UpdateChaOnline;
DROP PROCEDURE IF EXISTS GetInvenCount;
DROP PROCEDURE IF EXISTS DeleteGuildAlliance;
DROP PROCEDURE IF EXISTS InsertGuildAlliance;
DROP PROCEDURE IF EXISTS MakeUserInven;
DROP PROCEDURE IF EXISTS UpdateChaExp;
DROP PROCEDURE IF EXISTS UpdateChaHairColor;
DROP PROCEDURE IF EXISTS UpdateChaHairStyle;
DROP PROCEDURE IF EXISTS InsertChaFriend;
DROP PROCEDURE IF EXISTS sp_delete_guild_member;
DROP PROCEDURE IF EXISTS sp_DeletePet;

DELIMITER $$

CREATE PROCEDURE UpdateUserMoney(IN p_nUserNum INT, IN p_llMoney DECIMAL(19,4))
BEGIN
  UPDATE UserInven SET UserMoney=p_llMoney WHERE UserNum=p_nUserNum;
END$$

CREATE PROCEDURE InsertPartyMatch(IN p_nSGNum INT, IN p_nSvrNum INT, IN p_nWin INT, IN p_nLost INT)
BEGIN
  INSERT INTO LogPartyMatch (SGNum, SvrNum, Win, Lost)
  VALUES (p_nSGNum, p_nSvrNum, p_nWin, p_nLost);
END$$

CREATE PROCEDURE UpdateAllCharacterOffline()
BEGIN
  UPDATE ChaInfo SET ChaOnline=0 WHERE ChaOnline=1;
END$$

CREATE PROCEDURE UpdateChaOnline(IN p_nChaNum INT, IN p_nChaOnline INT)
BEGIN
  UPDATE ChaInfo SET ChaOnline=p_nChaOnline WHERE ChaNum=p_nChaNum;
END$$

CREATE PROCEDURE GetInvenCount(IN p_nUserNum INT, OUT p_nReturn INT)
BEGIN
  SET p_nReturn = 0;
  IF EXISTS (SELECT UserInvenNum FROM UserInven WHERE UserNum=p_nUserNum) THEN
    SET p_nReturn = 1;
  ELSE
    SET p_nReturn = 0;
  END IF;
END$$

CREATE PROCEDURE DeleteGuildAlliance(IN p_nGuNumP INT, IN p_nGuNumS INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_rc INT DEFAULT 0;
  DECLARE v_err INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  DELETE FROM GuildAlliance WHERE GuNumP=p_nGuNumP AND GuNumS=p_nGuNumS;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN SET p_nReturn = -1; ELSE SET p_nReturn = 0; END IF;
END$$

CREATE PROCEDURE InsertGuildAlliance(IN p_nGuNump INT, IN p_nGuNumS INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_rc INT DEFAULT 0;
  DECLARE v_err INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  INSERT INTO GuildAlliance (GuNumP, GuNumS) VALUES (p_nGuNump, p_nGuNumS);
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN SET p_nReturn = -1; ELSE SET p_nReturn = 0; END IF;
END$$

CREATE PROCEDURE MakeUserInven(IN p_nSGNum INT, IN p_nUserNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_rc INT DEFAULT 0;
  DECLARE v_err INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  INSERT INTO UserInven (SGNum, UserNum, UserMoney, UserInven) VALUES (p_nSGNum, p_nUserNum, 0, '');
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN SET p_nReturn = -1; ELSE SET p_nReturn = 0; END IF;
END$$

CREATE PROCEDURE UpdateChaExp(IN p_llExp DECIMAL(19,4), IN p_nChaNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_rc INT DEFAULT 0;
  DECLARE v_err INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  UPDATE ChaInfo SET ChaExp=p_llExp WHERE ChaNum=p_nChaNum;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN SET p_nReturn = -1; ELSE SET p_nReturn = 0; END IF;
END$$

CREATE PROCEDURE UpdateChaHairColor(IN p_nChaNum INT, IN p_nChaHairColor INT, OUT p_nReturn INT)
BEGIN
  SET p_nReturn = 0;
  IF EXISTS(SELECT ChaNum FROM ChaInfo WHERE ChaNum=p_nChaNum) THEN
    UPDATE ChaInfo SET ChaHairColor=p_nChaHairColor WHERE ChaNum=p_nChaNum;
    SET p_nReturn = 0;
  ELSE
    SET p_nReturn = -1;
  END IF;
END$$

CREATE PROCEDURE UpdateChaHairStyle(IN p_nChaNum INT, IN p_nChaHairStyle INT, OUT p_nReturn INT)
BEGIN
  SET p_nReturn = 0;
  IF EXISTS(SELECT ChaNum FROM ChaInfo WHERE ChaNum=p_nChaNum) THEN
    UPDATE ChaInfo SET ChaHair=p_nChaHairStyle WHERE ChaNum=p_nChaNum;
    SET p_nReturn = 0;
  ELSE
    SET p_nReturn = -1;
  END IF;
END$$

CREATE PROCEDURE InsertChaFriend(IN p_nChaP INT, IN p_nChaS INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_rc INT DEFAULT 0;
  DECLARE v_err INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  INSERT INTO ChaFriend (ChaP, ChaS) VALUES (p_nChaP, p_nChaS);
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN SET p_nReturn = -1; ELSE SET p_nReturn = 0; END IF;
END$$

CREATE PROCEDURE sp_delete_guild_member(IN p_ChaNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_rc INT DEFAULT 0;
  DECLARE v_err INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  UPDATE ChaInfo SET GuNum=0, GuPosition=0, ChaGuSecede=NOW() WHERE ChaNum=p_ChaNum;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN SET p_nReturn = 0; ELSE SET p_nReturn = 1; END IF;
END$$

CREATE PROCEDURE sp_DeletePet(IN p_nChaNum INT, IN p_nPetNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_rc INT DEFAULT 0;
  DECLARE v_err INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  START TRANSACTION;
  UPDATE PetInfo SET PetDeleted=1, PetDeletedDate=NOW() WHERE PetNum=p_nPetNum AND PetChaNum=p_nChaNum;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN
    ROLLBACK;
    SET p_nReturn = -1;
  ELSE
    COMMIT;
    SET p_nReturn = 0;
  END IF;
END$$

DELIMITER ;
