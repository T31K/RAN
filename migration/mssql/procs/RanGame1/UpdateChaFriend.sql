

-- DB_ERROR -1
-- DB_OK 0
CREATE PROCEDURE [dbo].[UpdateChaFriend]
	@nChaP int,
	@nChaS int,
	@nFlag int,
	@nReturn int OUTPUT
AS	
	DECLARE
		@error_var int, 
		@rowcount_var int
		
	SET NOCOUNT ON
	
	SET @nReturn = 0
	
	UPDATE ChaFriend
	SET ChaFlag=@nFlag
	WHERE ChaP=@nChaP AND ChaS=@nChaS
	
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


