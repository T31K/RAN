

CREATE PROCEDURE [dbo].[sp_SelectPet]
	@nPetNum		int,
	@nReturn		int OUTPUT
AS
	DECLARE
		@error_var int, 
		@rowcount_var int
		
	SET NOCOUNT ON
	
	SET @nReturn = 0	
	
	SELECT PetNum, PetName, PetChaNum, PetType, PetMID, PetSID, PetStyle, PetColor, PetFull, PetDeleted, PetCreateDate, PetDeletedDate
	FROM PetInfo
	WHERE PetNum=@nPetNum And PetDeleted=0

	SELECT @error_var = @@ERROR

	IF @error_var <> 0
	BEGIN
		SET @nReturn = -1
	END
	ELSE
	BEGIN
		SET @nReturn = 0
	END    

	SELECT PetInvenNum, PetNum, PetInvenType, PetInvenMID, PetInvenSID, PetInvenCMID, PetInvenCSID, PetInvenAvailable, PetInvenUpdateDate
	FROM PetInven
	Where PetNum=@nPetNum
	Order By PetInvenType, PetInvenMID, PetInvenSID

	SELECT @error_var = @error_var+@@ERROR, @rowcount_var = @@ROWCOUNT

	IF @error_var <> 0
	BEGIN
		SET @nReturn = -1
	END
	ELSE
	BEGIN
		SET @nReturn = 0
	END    


	SET NOCOUNT OFF
	RETURN @nReturn


