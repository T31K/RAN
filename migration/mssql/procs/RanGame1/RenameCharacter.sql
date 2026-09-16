

CREATE PROCEDURE [dbo].[RenameCharacter]
	@nChaNum int,
	@szChaName varchar(33),
	@nReturn int OUTPUT
AS	
	DECLARE
		@error_var int, 
		@rowcount_var int,
		@nChaNumTemp int
		
	SET NOCOUNT ON
	
	SET @nReturn = 0
	SET @nChaNumTemp = 0
	
	SELECT @nChaNumTemp=ChaNum FROM ChaInfo WHERE ChaName=@szChaName
	
	IF @nChaNumTemp <> 0 -- 某腐磐啊 捞固 粮犁
	BEGIN
		SET @nReturn = -1
		SET NOCOUNT OFF
		RETURN @nReturn	
	END
	
    UPDATE ChaInfo
    SET ChaName=@szChaName
    WHERE ChaNum=@nChaNum
    
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


