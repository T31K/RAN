
-- DB_ERROR -1
-- DB_OK 0

CREATE PROCEDURE [dbo].[sp_InsertChaLastInfo]
	@nChaNum int,
	@llChaLevel int,
	@llChaMoney money,
	@nReturn int OUTPUT
AS	
	DECLARE
		@error_var int, 
		@rowcount_var int
		
	SET NOCOUNT ON
	
	SET @nReturn = 0
	
	INSERT INTO ChaLastInfo (ChaNum, ChaLevel, ChaMoney) 
	VALUES(@nChaNum, @llChaLevel, @llChaMoney)
    
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


