

CREATE PROCEDURE [dbo].[GetInvenCount]    
    @nUserNum int,
	@nReturn int OUTPUT
AS		
	SET NOCOUNT ON
	
	SET @nReturn = 0		

	IF EXISTS (SELECT UserInvenNum FROM UserInven WHERE UserNum=@nUserNum) 
	BEGIN
		SET @nReturn = 1
	END
	ELSE
	BEGIN
        SET @nReturn = 0
	END
	SET NOCOUNT OFF

	RETURN @nReturn	


