-- RanGame1 stored procs batch C (guild / pet / vehicle / char / attend) MSSQL -> MariaDB
-- Params prefixed p_, locals v_. NOTE: rowcount checks use ROW_COUNT(); the DSN
-- must set FOUND_ROWS so it returns MATCHED (not just changed) rows like MSSQL @@ROWCOUNT.
USE RanGame1;

DROP PROCEDURE IF EXISTS sp_add_guild_member;
DROP PROCEDURE IF EXISTS sp_delete_guild_region;
DROP PROCEDURE IF EXISTS sp_InsertPet;
DROP PROCEDURE IF EXISTS RenameCharacter;
DROP PROCEDURE IF EXISTS sp_InsertVehicle;
DROP PROCEDURE IF EXISTS sp_add_guild_region;
DROP PROCEDURE IF EXISTS sp_delete_guild;
DROP PROCEDURE IF EXISTS sp_RenamePet;
DROP PROCEDURE IF EXISTS sp_InsertAttendLog;
DROP PROCEDURE IF EXISTS sp_create_guild;
DROP PROCEDURE IF EXISTS sp_EndGuBattle;
DROP PROCEDURE IF EXISTS sp_delete_character;
DROP PROCEDURE IF EXISTS sp_UpdatePetInven;

DELIMITER $$

CREATE PROCEDURE sp_add_guild_member(IN p_GuNum INT, IN p_ChaNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0; DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  UPDATE ChaInfo SET GuNum=p_GuNum WHERE ChaNum=p_ChaNum;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN SET p_nReturn = 0; ELSE SET p_nReturn = 1; END IF;
END$$

CREATE PROCEDURE sp_delete_guild_region(IN p_RegionID INT, IN p_GuNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0; DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  IF EXISTS(SELECT 1 FROM GuildRegion WHERE RegionID=p_RegionID AND GuNum=p_GuNum) THEN
    UPDATE GuildRegion SET GuNum=0, RegionTax=0 WHERE RegionID=p_RegionID;
    SET v_rc = ROW_COUNT();
    IF v_err <> 0 OR v_rc = 0 THEN SET p_nReturn = 0; ELSE SET p_nReturn = 1; END IF;
  ELSE
    SET p_nReturn = 0;
  END IF;
END$$

CREATE PROCEDURE sp_InsertPet(
  IN p_szPetName VARCHAR(20), IN p_nPetChaNum INT, IN p_nPetType INT, IN p_nPetMID INT,
  IN p_nPetSID INT, IN p_nPetCardMID INT, IN p_nPetCardSID INT, IN p_nPetStyle INT,
  IN p_nPetColor INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0; DECLARE v_id INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  START TRANSACTION;
  INSERT INTO PetInfo (PetName, PetChaNum, PetType, PetMID, PetSID, PetCardMID, PetCardSID, PetStyle, PetColor, PetPutOnItems)
    VALUES (p_szPetName, p_nPetChaNum, p_nPetType, p_nPetMID, p_nPetSID, p_nPetCardMID, p_nPetCardSID, p_nPetStyle, p_nPetColor, '');
  IF v_err <> 0 THEN
    ROLLBACK; SET p_nReturn = -1;
  ELSE
    SET v_id = LAST_INSERT_ID();
    UPDATE PetInfo SET PetNum=v_id WHERE PetUniqueNum=v_id;
    COMMIT;
    SET p_nReturn = v_id;
  END IF;
END$$

CREATE PROCEDURE RenameCharacter(IN p_nChaNum INT, IN p_szChaName VARCHAR(33), OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0; DECLARE v_rc INT DEFAULT 0; DECLARE v_temp INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  SELECT ChaNum INTO v_temp FROM ChaInfo WHERE ChaName=p_szChaName LIMIT 1;
  IF v_temp <> 0 THEN
    SET p_nReturn = -1;
  ELSE
    UPDATE ChaInfo SET ChaName=p_szChaName WHERE ChaNum=p_nChaNum;
    SET v_rc = ROW_COUNT();
    IF v_err <> 0 OR v_rc = 0 THEN SET p_nReturn = -1; ELSE SET p_nReturn = 0; END IF;
  END IF;
END$$

CREATE PROCEDURE sp_InsertVehicle(
  IN p_szVehicleName VARCHAR(20), IN p_nVehicleChaNum INT, IN p_nVehicleType INT,
  IN p_nVehicleCardMID INT, IN p_nVehicleCardSID INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0; DECLARE v_id INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  START TRANSACTION;
  INSERT INTO VehicleInfo (VehicleName, VehicleChaNum, VehicleType, VehicleCardMID, VehicleCardSID, VehiclePutOnItems)
    VALUES (p_szVehicleName, p_nVehicleChaNum, p_nVehicleType, p_nVehicleCardMID, p_nVehicleCardSID, '');
  IF v_err <> 0 THEN
    ROLLBACK; SET p_nReturn = -1;
  ELSE
    SET v_id = LAST_INSERT_ID();
    UPDATE VehicleInfo SET VehicleNum=v_id WHERE VehicleUniqueNum=v_id;
    COMMIT;
    SET p_nReturn = v_id;
  END IF;
END$$

CREATE PROCEDURE sp_add_guild_region(IN p_RegionID INT, IN p_GuNum INT, IN p_RegionTax DOUBLE, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0; DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  IF EXISTS(SELECT 1 FROM GuildRegion WHERE RegionID=p_RegionID) THEN
    UPDATE GuildRegion SET GuNum=p_GuNum, RegionTax=p_RegionTax WHERE RegionID=p_RegionID;
    SET v_rc = ROW_COUNT();
    IF v_err <> 0 OR v_rc = 0 THEN SET p_nReturn = 0; END IF;
  ELSE
    INSERT INTO GuildRegion (RegionID, GuNum, RegionTax) VALUES (p_RegionID, p_GuNum, p_RegionTax);
    SET v_rc = ROW_COUNT();
    IF v_err <> 0 OR v_rc = 0 THEN SET p_nReturn = 0; END IF;
  END IF;
  IF p_nReturn <> 0 THEN SET p_nReturn = 1; END IF;
END$$

CREATE PROCEDURE sp_delete_guild(IN p_GuNum INT, IN p_ChaNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0; DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  DELETE FROM GuildInfo WHERE GuNum=p_GuNum AND ChaNum=p_ChaNum;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN
    SET p_nReturn = 0;
  ELSE
    UPDATE ChaInfo SET GuNum=0, GuPosition=0 WHERE GuNum=p_GuNum;
    DELETE FROM GuildAlliance WHERE GuNumP=p_GuNum OR GuNumS=p_GuNum;
    SET p_nReturn = 1;
  END IF;
END$$

CREATE PROCEDURE sp_RenamePet(IN p_nChaNum INT, IN p_nPetNum INT, IN p_szPetName VARCHAR(20), OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0; DECLARE v_rc INT DEFAULT 0; DECLARE v_temp INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  SELECT PetNum INTO v_temp FROM PetInfo WHERE PetName=p_szPetName LIMIT 1;
  IF v_temp <> 0 THEN
    SET p_nReturn = -1;
  ELSE
    START TRANSACTION;
    UPDATE PetInfo SET PetName=p_szPetName WHERE PetNum=p_nPetNum AND PetChaNum=p_nChaNum;
    SET v_rc = ROW_COUNT();
    IF v_err <> 0 OR v_rc = 0 THEN
      ROLLBACK; SET p_nReturn = -1;
    ELSE
      COMMIT; SET p_nReturn = 0;
    END IF;
  END IF;
END$$

CREATE PROCEDURE sp_InsertAttendLog(IN p_UserNum INT, IN p_nCount INT, IN p_nReward INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0; DECLARE v_exist INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = -1;
  START TRANSACTION;
  SELECT COUNT(1) INTO v_exist FROM Attendance WHERE UserNum=p_UserNum;
  IF v_exist = 0 THEN
    INSERT INTO Attendance (UserNum, DaysCount, RewardCount, AttendDate)
      VALUES (p_UserNum, 1, p_nReward, NOW());
  ELSE
    UPDATE Attendance SET DaysCount=p_nCount, RewardCount=p_nReward, AttendDate=NOW() WHERE UserNum=p_UserNum;
  END IF;
  IF v_err = 0 THEN
    COMMIT; SET p_nReturn = 0;
  ELSE
    ROLLBACK;
  END IF;
END$$

CREATE PROCEDURE sp_create_guild(IN p_ChaNum INT, IN p_GuName CHAR(33), OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0; DECLARE v_GuNum INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  SELECT GuNum INTO v_GuNum FROM GuildInfo WHERE ChaNum=p_ChaNum LIMIT 1;
  IF v_GuNum <> 0 THEN
    SET p_nReturn = -1;
  ELSE
    INSERT INTO GuildInfo (ChaNum, GuName) VALUES (p_ChaNum, p_GuName);
    IF v_err <> 0 THEN
      SET p_nReturn = -2;
    ELSE
      SET v_GuNum = LAST_INSERT_ID();
      SET p_nReturn = v_GuNum;
      UPDATE ChaInfo SET GuNum=v_GuNum WHERE ChaNum=p_ChaNum;
    END IF;
  END IF;
END$$

CREATE PROCEDURE sp_EndGuBattle(
  IN p_GuSNum INT, IN p_GuPNum INT, IN p_GuFlag INT, IN p_GuKillNum INT, IN p_GuDeathNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0; DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  IF p_GuFlag = 6 THEN
    UPDATE GuildInfo SET GuBattleWin = GuBattleWin + 1 WHERE GuNum=p_GuSNum;
  ELSEIF p_GuFlag = 5 THEN
    UPDATE GuildInfo SET GuBattleLose = GuBattleLose + 1 WHERE GuNum=p_GuSNum;
  ELSEIF p_GuFlag = 1 THEN
    UPDATE GuildInfo SET GuBattleDraw = GuBattleDraw + 1 WHERE GuNum=p_GuPNum;
  END IF;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN SET p_nReturn = 0; ELSE SET p_nReturn = 1; END IF;
END$$

CREATE PROCEDURE sp_delete_character(IN p_ChaNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_GuNum INT DEFAULT 0; DECLARE v_ChaDeleted INT DEFAULT 0; DECLARE v_Extreme INT DEFAULT 0;
  SET p_nReturn = 0;
  SELECT ChaDeleted INTO v_ChaDeleted FROM ChaInfo WHERE ChaNum=p_ChaNum LIMIT 1;
  IF v_ChaDeleted = 1 THEN
    SET p_nReturn = -1;
  ELSE
    SELECT GuNum INTO v_GuNum FROM GuildInfo WHERE ChaNum=p_ChaNum LIMIT 1;
    IF v_GuNum <> 0 THEN
      SET p_nReturn = -2;
    ELSE
      SELECT ChaClass INTO v_Extreme FROM ChaInfo WHERE ChaNum=p_ChaNum LIMIT 1;
      UPDATE ChaInfo SET ChaDeleted=1, ChaDeletedDate=NOW() WHERE ChaNum=p_ChaNum;
      IF v_Extreme = 16 THEN SET p_nReturn = 1;
      ELSEIF v_Extreme = 32 THEN SET p_nReturn = 2;
      ELSE SET p_nReturn = 0; END IF;
    END IF;
  END IF;
END$$

CREATE PROCEDURE sp_UpdatePetInven(
  IN p_nChaNum INT, IN p_nPetNum INT, IN p_nPetInvenType INT, IN p_nPetInvenMID INT,
  IN p_nPetInvenSID INT, IN p_nPetInvenCMID INT, IN p_nPetInvenCSID INT, IN p_nPetInvenAvailable INT,
  OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0; DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  START TRANSACTION;
  IF p_nPetInvenType = 1 OR p_nPetInvenType = 2 THEN
    UPDATE PetInven SET PetInvenMID=p_nPetInvenMID, PetInvenSID=p_nPetInvenSID,
      PetInvenCMID=p_nPetInvenCMID, PetInvenCSID=p_nPetInvenCSID, PetInvenUpdateDate=NOW()
      WHERE PetNum=p_nPetNum AND PetChaNum=p_nChaNum AND PetInvenType=p_nPetInvenType;
  ELSEIF p_nPetInvenType = 3 THEN
    UPDATE PetInven SET PetInvenCMID=p_nPetInvenCMID, PetInvenCSID=p_nPetInvenCSID,
      PetInvenAvailable=p_nPetInvenAvailable, PetInvenUpdateDate=NOW()
      WHERE PetNum=p_nPetNum AND PetChaNum=p_nChaNum AND PetInvenType=p_nPetInvenType
        AND PetInvenMID=p_nPetInvenMID AND PetInvenSID=p_nPetInvenSID;
  END IF;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 THEN
    ROLLBACK; SET p_nReturn = -1;
  ELSE
    COMMIT; SET p_nReturn = 0;
  END IF;
  IF v_err = 0 AND v_rc = 0 THEN
    START TRANSACTION;
    INSERT INTO PetInven (PetNum, PetChaNum, PetInvenType, PetInvenMID, PetInvenSID, PetInvenCMID, PetInvenCSID, PetInvenAvailable)
      VALUES (p_nPetNum, p_nChaNum, p_nPetInvenType, p_nPetInvenMID, p_nPetInvenSID, p_nPetInvenCMID, p_nPetInvenCSID, p_nPetInvenAvailable);
    SET v_rc = ROW_COUNT();
    IF v_err <> 0 OR v_rc = 0 THEN
      ROLLBACK; SET p_nReturn = -1;
    ELSE
      COMMIT; SET p_nReturn = 0;
    END IF;
  END IF;
END$$

DELIMITER ;
