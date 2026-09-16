


CREATE PROCEDURE [dbo].[InsertLogHackProgram]
	@nSGNum int,
	@nSvrNum int,
	@nUserNum int,
	@nChaNum int,
	@nHackProgramNum int,
	@strComment varchar (512),
	@nReturn int OUTPUT
AS	
	DECLARE
		@error_var int, 
		@rowcount_var int
		
	SET NOCOUNT ON
	
	SET @nReturn = 0

    INSERT INTO LogHackProgram (SGNum,   SvrNum,   UserNum,   ChaNum,   HackProgramNum,   HackComment) 
    VALUES (@nSGNum, @nSvrNum, @nUserNum, @nChaNum, @nHackProgramNum, @strComment)
    
    SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT    
    IF @error_var <> 0 OR @rowcount_var = 0
    BEGIN
        SET @nReturn = -1
        SET NOCOUNT OFF
        RETURN @nReturn
    END
    ELSE
    BEGIN
        SET @nReturn = 0
        SET NOCOUNT OFF
        RETURN @nReturn
    END



