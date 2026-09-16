

CREATE PROCEDURE [dbo].[sp_InsertAttendLog]
	@UserNum	int,
	@nCount	int,
	@nReward int,
	@nReturn	int OUTPUT
AS	

/*
Created by PrinceOfPersia
August 8, 2011
*/

DECLARE @exist int;
		
SET NOCOUNT ON;

SET @nReturn = -1;

BEGIN TRAN

SELECT @EXIST =  COUNT(1) FROM Attendance(NOLOCK) WHERE UserNum = @UserNum;

IF @EXIST= 0
BEGIN
	INSERT INTO Attendance(
	UserNum,
	DaysCount,
	RewardCount,
	AttendDate
	)
	VALUES (
	@UserNum,
	1,
	@nReward,
	GETDATE()
	)		
END
ELSE
BEGIN 
	UPDATE Attendance 
	SET DaysCount = @nCount, 
	RewardCount = @nReward,
	AttendDate = GETDATE()
	WHERE UserNum = @UserNum;
END

IF @@ERROR = 0 
	BEGIN 
		COMMIT TRAN;
		SET @nReturn = 0;
	END

SET NOCOUNT OFF;
RETURN @nReturn


