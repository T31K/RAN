
CREATE PROCEDURE [dbo].[sp_UpdateChaGender]
	@nChaNum int,
	@nChaClass int,
	@nChaSex int,
	@nFace int,
	@nChaHair int,
	@nChaHairColor int,
	@nReturn int OUTPUT
AS
	DECLARE
		@error_var int, 
		@rowcount_var int,
		@nChaNumTemp int
		
	SET NOCOUNT ON
	
	SET @nReturn=0
	SET @nChaNumTemp=0
	
	IF EXISTS(SELECT ChaNum FROM ChaInfo WHERE ChaNum=@nChaNum)
	BEGIN
		UPDATE ChaInfo SET ChaClass=@nChaClass, ChaSex=@nChaSex, ChaFace=@nFace, ChaHair=@nChaHair, ChaHairColor=@nChaHairColor
		WHERE ChaNum=@nChaNum
		
		SET @nReturn=0
		SET NOCOUNT OFF
		RETURN @nReturn
	END
	ELSE
	BEGIN
		SET @nReturn=-1
		SET NOCOUNT OFF
		RETURN @nReturn
	END


