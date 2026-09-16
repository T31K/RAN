


CREATE PROCEDURE [dbo].[sp_InsertPet]
	@szPetName	  varchar(20),
	@nPetChaNum	  int,
	@nPetType	  int,
	@nPetMID	  int,
	@nPetSID	  int,
	@nPetCardMID       int,
	@nPetCardSID  	  int,
	@nPetStyle	  int,
	@nPetColor	  int,
	@nReturn	  int OUTPUT
As
	DECLARE
		@error_var int, 
		@rowcount_var int

	SET NOCOUNT ON
	SET @nReturn = 0

	BEGIN TRAN

	Insert Into PetInfo (PetName, PetChaNum, PetType, PetMID, PetSID, PetCardMID, PetCardSID, PetStyle, PetColor, PetPutOnItems)
	Values (@szPetName, @nPetChaNum, @nPetType, @nPetMID, @nPetSID, @nPetCardMID, @nPetCardSID, @nPetStyle, @nPetColor, '')
	
	SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT    
	IF @error_var <> 0 OR @rowcount_var = 0
	BEGIN
		ROLLBACK TRAN
		SET @nReturn = -1
	END
	ELSE
	BEGIN
		COMMIT TRAN
		UPDATE PetInfo SET PetNum = @@IDENTITY WHERE PetUniqueNum = @@IDENTITY
		SET @nReturn = @@IDENTITY
	END

	SET NOCOUNT OFF
	RETURN @nReturn


